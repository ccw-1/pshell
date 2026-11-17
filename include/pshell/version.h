#ifndef PSHELL_VERSION_H
#define PSHELL_VERSION_H

/*
 * pshell version.
 *
 * The VERSION file at the repository root is the single source of truth.
 * The Makefile generates pshell/version_gen.h from it (and from
 * `git describe`), which defines:
 *
 *   PSHELL_VERSION_MAJOR / _MINOR / _PATCH   integers
 *   PSHELL_VERSION_STRING                    "MAJOR.MINOR.PATCH"
 *   PSHELL_BUILD_INFO                        git describe output, e.g.
 *                                            "v1.3.0" or
 *                                            "v1.3.0-4-g1a2b3c4-dirty",
 *                                            "unknown" outside a git tree
 *
 * To release, use scripts/release.sh (do not edit the version by hand).
 *
 * Bump rules (semantic versioning):
 *   MAJOR - incompatible API or protocol changes
 *   MINOR - new features, backward compatible
 *   PATCH - backward compatible bug fixes
 */

#include "pshell/version_gen.h"

#define PSHELL_VERSION_NUMERIC \
    ((PSHELL_VERSION_MAJOR * 10000) + \
     (PSHELL_VERSION_MINOR * 100) + \
      PSHELL_VERSION_PATCH)

#endif /* PSHELL_VERSION_H */
