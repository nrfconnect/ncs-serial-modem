---
applyTo: "**/*.c,**/*.h"
---
# C Code Guidelines

## Embedded C Programming Guidelines

### C Language Best Practices
- **Use `const`** for read-only data to prevent accidental modifications
- **Use `static`** for local scope variables and functions to limit visibility
- **Add error handling** for all operations (use common sense for scope)
- **Use assertions** for debugging and validation
- **Initialize all variables** to prevent undefined behavior
- **Avoid floating-point operations** when possible for embedded systems
- **Use bit fields** for memory efficiency in structures

### Clean Code Principles
- **Avoid magic numbers**: Use defines, enums, or Kconfig options instead
- **Keep functions small and focused**: Single responsibility principle
- **Use clear naming conventions**: Include module prefixes for clarity
- **Document all public APIs**: Provide clear function descriptions
- **Avoid unnecessary comments**: Let code be self-documenting

### NCS Coding Standards
- Follow contribution guidelines: [contributions.rst](https://github.com/nrfconnect/sdk-nrf/blob/main/doc/nrf/dev_model_and_contributions/contributions.rst)
- Follow Zephyr coding guidelines: [index.rst](https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/contribute/index.rst)
- Reference additional guidelines in:
  - `nrf/doc/nrf/dev_model_and_contributions`
  - `zephyr/doc/contribute`

### Memory and Performance
- Optimize for memory usage in embedded environments
- Consider stack usage and avoid large local variables
- Use appropriate data types for the target platform

### Correctness standard

When writing or reviewing C code, apply the **SEI CERT C Coding Standard** (https://cmu-sei.github.io/secure-coding-standards/sei-cert-c-coding-standard/) as the primary reference for correctness. Flag violations with the relevant rule ID. Key rule categories:

- **INT** — integer operations (overflow, conversion, shifts)
- **MEM** — memory management (allocation, deallocation, use-after-free)
- **EXP** — expressions (undefined behavior, side effects)
- **ARR** — array bounds and pointer arithmetic
- **STR** — string handling
- **CON** — concurrency and interrupt safety
- **ERR** — error handling
