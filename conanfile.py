from conan import ConanFile


class GdalAsyncDependencies(ConanFile):
    """
    Dependency manifest for the gdal-async native addon.

    This replaces the vendored sources under deps/, which used to be downloaded
    by deps/*.sh and then converted to gyp by hand. GDAL brings in the rest of
    the graph (proj, geos, hdf5, netcdf, curl, expat, sqlite3, openjpeg, libaec
    ...) through its own recipe, so only the top-level library is listed.

    Usage:

        conan install . -of build-conan -s build_type=Release
        conan build   . -of build-conan          # or the cmake-js preset

    The CMake side picks the package up through find_package(gdal CONFIG), which
    is what the CMakeDeps generator provides.

    GDAL itself can come from conan-center or from a tree you built yourself -
    see conan/recipes/gdal-prebuilt for the latter:

        GDAL_LOCAL_ROOT=/path/to/gdal-prefix \\
            conan export-pkg conan/recipes/gdal-prebuilt --version=3.13.0
    """

    settings = "os", "arch", "compiler", "build_type"

    generators = "CMakeToolchain", "CMakeDeps"

    def requirements(self):
        # The version the addon is developed and tested against - it is also the
        # version of the vendored sources that used to live under deps/libgdal.
        # A version range is not an option: Conan resolves it to the newest
        # recipe it knows, which may not have a binary for the current
        # toolchain. Edit this line (or package your own GDAL with
        # conan/recipes/gdal-prebuilt) when the toolchain cannot consume it.
        self.requires("gdal/3.13.0")
