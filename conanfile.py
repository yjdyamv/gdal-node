from conan import ConanFile


class GdalAsyncDependencies(ConanFile):
    """
    Dependency manifest for the gdal-async native addon.

    This replaces the vendored sources under deps/, which used to be downloaded
    by deps/*.sh and then converted to gyp by hand. GDAL brings in the rest of
    the graph (proj, geos, hdf5, netcdf, curl, expat, sqlite3, openjpeg, libaec
    ...) through its own recipe, so only the top-level libraries are listed.

    Consumer usage:

        conan install . -of build-conan -s build_type=Release
        cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=build-conan/conan_toolchain.cmake ..

    The CMake side picks the package up through find_package(gdal CONFIG), which
    is what the CMakeDeps generator provides.
    """

    settings = "os", "arch", "compiler", "build_type"

    generators = "CMakeToolchain", "CMakeDeps"

    def requirements(self):
        self.requires("gdal/3.13.0")
