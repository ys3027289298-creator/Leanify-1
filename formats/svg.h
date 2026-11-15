#ifndef FORMATS_SVG_H_
#define FORMATS_SVG_H_

#include <cstddef>

#include <pugixml/pugixml.hpp>

// Maximum number of fractional digits kept when trimming numeric precision.
// A negative value (the default) disables precision trimming entirely, which
// keeps the legacy byte-identical output for files the other passes cannot
// shrink. Configurable on the command line with --svg-precision.
extern int svg_precision;

// Runs the lossless SVG passes against |root| (the <svg> document element):
//   1. comments are already dropped by the parser, this is asserted here,
//   2. duplicate <defs> entries are merged and references are rewritten,
//   3. numeric precision is trimmed, capped at |svg_precision| digits.
// Features that cannot be handled safely make the containing subtree skip the
// destructive passes; each skip is recorded on stderr in verbose mode.
// Returns the number of skipped (element, feature) pairs.
size_t SvgLeanify(pugi::xml_node root);

#endif  // FORMATS_SVG_H_
