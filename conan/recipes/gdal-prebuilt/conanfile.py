import glob
import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy


class GdalPrebuiltConan(ConanFile):
    """
    Packages an already-built GDAL install tree as a Conan package.

    This is the bridge for the configurations where GDAL cannot come from
    conan-center: there is no prebuilt binary for the toolchain, or the project
    needs its own format set and patches. Point GDAL_LOCAL_ROOT at an install
    prefix - the layout the vendored deps/libgdal.sh produces, or a distro
    package extracted into a directory - and export it:

        GDAL_LOCAL_ROOT=/path/to/prefix \\
            conan export-pkg conan/recipes/gdal-prebuilt --version=3.12.2 \\
            -s build_type=Release

    The recipe carries no version of its own, so --version decides what the
    resulting package is called; it must match the GDAL in the tree.

    The distro layout is <root>/usr/{include,lib/<multiarch>,share}. Everything
    under usr/lib is flattened into lib/, because a distro libgdal.so depends on
    its sibling shared libraries and they have to stay next to it. The data
    directories land in res/ so a consumer can point GDAL_DATA and PROJ_LIB at
    them.
    """

    name = "gdal"
    package_type = "shared-library"

    settings = "os", "arch", "compiler", "build_type"

    def package(self):
        root = os.environ.get("GDAL_LOCAL_ROOT")
        if not root or not os.path.isdir(root):
            raise ConanInvalidConfiguration(
                "GDAL_LOCAL_ROOT must point at the GDAL install prefix (it is "
                "read by package(), so it has to be set for export-pkg)")

        include = os.path.join(root, "usr", "include")
        if not os.path.isdir(include):
            raise ConanInvalidConfiguration(f"{include} does not exist")

        # keeps the gdal/ and proj/ subdirectories
        copy(self, "*.h", src=include, dst=os.path.join(self.package_folder, "include"))

        libdirs = [os.path.join(root, "usr", "lib")] + glob.glob(os.path.join(root, "usr", "lib", "*"))
        libs = 0
        for libdir in libdirs:
            if not os.path.isdir(libdir):
                continue
            if glob.glob(os.path.join(libdir, "libgdal.so*")):
                copy(self, "*.so*", src=libdir, dst=os.path.join(self.package_folder, "lib"))
                libs += 1
        if libs == 0:
            raise ConanInvalidConfiguration(f"no libgdal.so* under {root}/usr/lib")

        for name in ("gdal", "proj"):
            data = os.path.join(root, "usr", "share", name)
            if os.path.isdir(data):
                copy(self, "*", src=data, dst=os.path.join(self.package_folder, "res", name))

    def package_info(self):
        # <gdal_priv.h> and friends live in the gdal/ subdirectory, while proj.h
        # sits directly in include/
        self.cpp_info.includedirs = ["include", "include/gdal"]
        self.cpp_info.libdirs = ["lib"]
        self.cpp_info.libs = ["gdal"]
        self.cpp_info.resdirs = ["res"]

        # the tree is relocatable, so both libraries only find their data files
        # if the environment says where they are (conanrun.sh carries these)
        for var, name in (("GDAL_DATA", "gdal"), ("PROJ_LIB", "proj")):
            data = os.path.join(self.package_folder, "res", name)
            if os.path.isdir(data):
                self.runenv_info.define_path(var, data)
