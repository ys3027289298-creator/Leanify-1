#include "svg.h"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../utils.h"

int svg_precision = -1;

namespace {

using std::map;
using std::set;
using std::string;
using std::vector;

// Element names that can be referenced from paint servers / geometry and are
// safe to merge when their content is identical apart from their id.
const set<string> kDeduplicatable = {
  "linearGradient", "radialGradient", "pattern", "filter", "marker", "mask", "symbol", "clipPath"
};

// Subtrees containing these elements are left byte-for-byte untouched by the
// destructive passes because their semantics cannot be compared or rewritten
// safely from the DOM alone.
const set<string> kUnsupported = {
  "foreignObject", "script", "style", "animate", "animateMotion", "animateTransform", "set"
};

// Attributes whose values are pure numbers or number lists in user units.
// Anything not on this list (colors, identifiers, font names, style text...)
// is never touched by the precision pass.
const set<string> kNumericAttributes = {
  "x", "y", "x1", "y1", "x2", "y2", "cx", "cy", "r", "rx", "ry", "fx", "fy",
  "width", "height", "dx", "dy", "points", "d", "stroke-width", "stroke-miterlimit",
  "offset", "gradientTransform", "patternTransform", "transform"
};

struct StringWriter : pugi::xml_writer {
  string buf;
  void write(const void* data, size_t size) override {
    buf.append(static_cast<const char*>(data), size);
  }
};

// Serializes a node without the root element's own id attribute so that two
// referenced resources with different ids but identical bodies compare equal.
// Ids on descendants are preserved: stripping them too could merge resources
// whose inner elements carry distinct ids that other parts of the document
// reference. Attribute order is stable in pugixml (document order), and the
// merge only happens inside one document, so the canonical text is a reliable
// structural fingerprint.
string Canonical(pugi::xml_node node) {
  StringWriter writer;
  node.print(writer, "  ");
  string out;
  out.reserve(writer.buf.size());
  size_t open_tag_end = writer.buf.find('>');
  if (open_tag_end == string::npos)
    return writer.buf;
  for (size_t i = 0; i < writer.buf.size();) {
    static const char kId[] = "id=\"";
    size_t hit = writer.buf.find(kId, i);
    if (hit == string::npos) {
      out.append(writer.buf, i, string::npos);
      break;
    }
    // Only the opening tag of the root element is eligible for id stripping.
    if (hit >= open_tag_end) {
      out.append(writer.buf, i, string::npos);
      break;
    }
    // Only treat the match as an attribute when preceded by whitespace,
    // otherwise it could be the tail of another attribute name.
    if (hit > 0 && writer.buf[hit - 1] != ' ' && writer.buf[hit - 1] != '\t' &&
        writer.buf[hit - 1] != '\n' && writer.buf[hit - 1] != '\r') {
      out.append(writer.buf, i, hit - i + 1);
      i = hit + 1;
      continue;
    }
    size_t close = writer.buf.find('"', hit + sizeof(kId) - 1);
    if (close == string::npos) {
      out.append(writer.buf, i, string::npos);
      break;
    }
    out.append(writer.buf, i, hit - i);
    i = close + 1;
  }
  return out;
}

// Rewrites every reference to an old id to the surviving id. Both plain
// fragment references (href="#old") and functional paint references
// (fill="url(#old)"), including CSS inside the style attribute, are covered.
void RewriteReferences(pugi::xml_node node, const map<string, string>& renamed) {
  static const char* kRefAttrs[] = { "href", "xlink:href", "fill", "stroke", "clip-path",
                                     "mask", "filter", "marker-start", "marker-mid",
                                     "marker-end", "style", "begin", "end" };
  for (pugi::xml_attribute attr : node.attributes()) {
    bool ref_attr = false;
    for (const char* name : kRefAttrs) {
      if (strcmp(attr.name(), name) == 0) {
        ref_attr = true;
        break;
      }
    }
    if (!ref_attr)
      continue;

    string value = attr.value();
    for (const auto& kv : renamed) {
      const string old_ref = "#" + kv.first;
      const string new_ref = "#" + kv.second;
      size_t pos = 0;
      while ((pos = value.find(old_ref, pos)) != string::npos) {
        value.replace(pos, old_ref.size(), new_ref);
        pos += new_ref.size();
      }
    }
    if (value != attr.value())
      attr.set_value(value.c_str());
  }
  for (pugi::xml_node child : node.children())
    RewriteReferences(child, renamed);
}

// Rounds a decimal token to at most |max_digits| fractional digits. The
// original spelling is kept whenever the rounding does not make the token
// shorter, so precision trimming can never grow the file.
string TrimNumber(const string& token, int max_digits) {
  size_t dot = token.find('.');
  if (dot == string::npos || dot + 1 >= token.size())
    return token;

  size_t frac_end = dot + 1;
  while (frac_end < token.size() && token[frac_end] >= '0' && token[frac_end] <= '9')
    ++frac_end;
  size_t frac_digits = frac_end - dot - 1;
  if (static_cast<int>(frac_digits) <= max_digits)
    return token;

  // Exponent notation is left byte-for-byte untouched: rounding the mantissa
  // would require rewriting the exponent too, and the render gate already
  // covers the common fixed-point cases.
  if (token.find('e') != string::npos || token.find('E') != string::npos)
    return token;

  // Values outside the exact-integer range of double cannot be round-tripped
  // safely; leave the original spelling alone rather than silently changing
  // the coordinate's magnitude.
  size_t int_digits = token[0] == '-' ? dot - 1 : dot;
  if (int_digits > 15)
    return token;

  double value = 0;
  try {
    value = stod(token);
  } catch (...) {
    return token;
  }

  char fmt[16];
  snprintf(fmt, sizeof(fmt), "%%.%df", max_digits);
  char buf[64];
  snprintf(buf, sizeof(buf), fmt, value);
  string rounded(buf);

  // Drop a trailing ".0" for integer results and strip trailing zeros.
  size_t rdot = rounded.find('.');
  if (rdot != string::npos) {
    size_t last_nonzero = rounded.find_last_not_of('0');
    if (last_nonzero == rdot)
      rounded.resize(rdot);
    else if (last_nonzero != string::npos && last_nonzero > rdot)
      rounded.resize(last_nonzero + 1);
  }
  // snprintf can produce "-0" or "-0.00" for small negative values; normalize
  // every negative-zero spelling back to "0".
  if (!rounded.empty() && rounded[0] == '-') {
    bool all_zero = true;
    for (char c : rounded.substr(1)) {
      if (c != '0' && c != '.')
        all_zero = false;
    }
    if (all_zero)
      rounded = "0";
  }
  return rounded.size() < token.size() ? rounded : token;
}

// Replaces every standalone decimal number in a coordinate/path value while
// leaving command letters, signs and exponent notation intact.
string TrimNumericValue(const string& value, int max_digits) {
  string out;
  out.reserve(value.size());
  size_t i = 0;
  while (i < value.size()) {
    char c = value[i];
    bool number_start = (c >= '0' && c <= '9') ||
                        (c == '.' && i + 1 < value.size() && value[i + 1] >= '0' && value[i + 1] <= '9') ||
                        (c == '-' && i + 1 < value.size() &&
                         ((value[i + 1] >= '0' && value[i + 1] <= '9') || value[i + 1] == '.'));
    if (!number_start) {
      out.push_back(c);
      ++i;
      continue;
    }
    // A leading sign belongs to the number token so negative values are
    // rounded (and negative-zero normalized) as a whole.
    size_t start = i;
    while (i < value.size() &&
           ((value[i] >= '0' && value[i] <= '9') || value[i] == '.' || value[i] == 'e' ||
            value[i] == 'E' || value[i] == '+' || value[i] == '-'))
      ++i;
    string token = value.substr(start, i - start);
    out += TrimNumber(token, max_digits);
  }
  return out;
}

void TrimPrecision(pugi::xml_node node, const set<pugi::xml_node>& protected_nodes) {
  if (protected_nodes.count(node))
    return;

  for (pugi::xml_attribute attr : node.attributes()) {
    if (!kNumericAttributes.count(attr.name()))
      continue;
    string value = attr.value();
    string trimmed = TrimNumericValue(value, svg_precision);
    if (trimmed.size() < value.size())
      attr.set_value(trimmed.c_str());
  }
  for (pugi::xml_node child : node.children())
    TrimPrecision(child, protected_nodes);
}

// Walks the tree once to collect (a) subtrees that contain unsupported
// features and (b) the feature names that triggered each skip.
void FindProtectedSubtrees(pugi::xml_node node, set<pugi::xml_node>& protected_nodes,
                           set<string>& skipped_features) {
  for (pugi::xml_node child : node.children())
    FindProtectedSubtrees(child, protected_nodes, skipped_features);

  if (kUnsupported.count(node.name())) {
    for (pugi::xml_node ancestor = node; ancestor; ancestor = ancestor.parent())
      protected_nodes.insert(ancestor);
    skipped_features.insert(node.name());
    if (is_verbose)
      VerbosePrint("SVG: skipping unsupported feature <", node.name(), ">, subtree kept unchanged.");
  }
}

}  // namespace

size_t SvgLeanify(pugi::xml_node root) {
  set<pugi::xml_node> protected_nodes;
  set<string> skipped_features;
  FindProtectedSubtrees(root, protected_nodes, skipped_features);

  // Merge duplicate referenced resources inside every <defs>.
  map<string, string> renamed;
  for (pugi::xml_node defs : root.children("defs")) {
    if (protected_nodes.count(defs))
      continue;

    map<string, pugi::xml_node> canonical_first;
    vector<pugi::xml_node> duplicates;
    for (pugi::xml_node child = defs.first_child(); child; child = child.next_sibling()) {
      if (!kDeduplicatable.count(child.name()) || !child.attribute("id"))
        continue;

      string fingerprint = Canonical(child);
      auto inserted = canonical_first.emplace(fingerprint, child);
      if (!inserted.second)
        duplicates.push_back(child);
    }

    for (pugi::xml_node dup : duplicates) {
      pugi::xml_node first = canonical_first[Canonical(dup)];
      renamed[dup.attribute("id").value()] = first.attribute("id").value();
      if (is_verbose)
        VerbosePrint("SVG: merging duplicate <", dup.name(), "> #", dup.attribute("id").value(), " -> #",
                     first.attribute("id").value());
      defs.remove_child(dup);
    }
  }
  if (!renamed.empty())
    RewriteReferences(root, renamed);

  if (svg_precision >= 0)
    TrimPrecision(root, protected_nodes);

  return skipped_features.size();
}
