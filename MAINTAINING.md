Maintaining
===========

Release checklist
-----------------

1. Update the project version in `meson.build` and `Doxyfile`.
2. Run shared and static builds.
3. Run GCC and Clang with warnings as errors.
4. Run AddressSanitizer and UndefinedBehaviorSanitizer builds.
5. Build manual pages and run Doxygen with warnings as errors.
6. Install into a temporary prefix and compile an external pkg-config client.
7. Confirm `default_library=both` is rejected.
8. Confirm reference tools remain uninstalled unless `install_tools=true`.
9. Review exported symbols; internal candidate fingerprints must remain hidden.
10. Tag the exact tested tree.

Compatibility
-------------

The public C++ API and pkg-config name are release contracts. Private staging
fingerprints and terminal implementation are not ABI.

The filesystem staging layout is a documented backend contract. Changes to
its path mapping or disposition semantics require migration notes and
regression tests.
