#ifndef PSHELL_VERSION_H
#define PSHELL_VERSION_H

/*
 * Single source of truth for the pshell version.
 * Mirrored in the VERSION file at the repository root.
 *
 * Bump rules (semantic versioning):
 *   MAJOR - incompatible API or protocol changes
 *   MINOR - new features, backward compatible
 *   PATCH - backward compatible bug fixes
 */

#define PSHELL_VERSION_MAJOR 1
#define PSHELL_VERSION_MINOR 2
#define PSHELL_VERSION_PATCH 0

#define PSHELL_VERSION_STRING "1.2.0"

/* Convenience macros for embedding the version in strings */
#define PSHELL_VERSION_STRINGIFY_(x) #x
#define PSHELL_VERSION_STRINGIFY(x) PSHELL_VERSION_STRINGIFY_(x)

#define PSHELL_VERSION_NUMERIC \
    ((PSHELL_VERSION_MAJOR * 10000) + \
     (PSHELL_VERSION_MINOR * 100) + \
      PSHELL_VERSION_PATCH)

#endif /* PSHELL_VERSION_H */
