# New ame game

Copy this directory, rename the executable and project in `CMakeLists.txt`,
and point `CMAKE_PREFIX_PATH` at an installed ame package:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/ame
cmake --build build
```

The template intentionally uses the low-level camera/render calls. One-call
boot and compatibility aliases are deliberately not part of the early API.
