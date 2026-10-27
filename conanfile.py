from conan import ConanFile
from conan.errors import ConanException
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain
from pathlib import Path


class CppTemplate(ConanFile):
    name = "cpp-template"
    version = "0.1.0"
    package_type = "application"
    required_conan_version = ">=2.2 <3"
    settings = "os", "compiler", "build_type", "arch"
    exports_sources = "CMakeLists.txt", "src/*", "inc/*", "config/*", "docs/*", "tests/*"
    default_options = {
        "drogon/*:with_boost": False,
        "trantor/*:with_spdlog": False,
        "spdlog/*:header_only": False,
    }
    def requirements(self):
        self.requires("spdlog/1.17.0")
        self.requires("drogon/1.9.13")

    def build_requirements(self):
        self.test_requires("gtest/1.18.0")

    def layout(self):
        self.folders.build = "build"
        self.folders.generators = "build/generators"

    def generate(self):
        CMakeDeps(self).generate()
        toolchain = CMakeToolchain(self, generator="Ninja")
        toolchain.variables["CPP_CONAN_BUILD_TYPE"] = str(self.settings.build_type)
        toolchain.variables["CMAKE_CXX_STANDARD"] = "20"
        toolchain.variables["CMAKE_CXX_STANDARD_REQUIRED"] = True
        toolchain.variables["CMAKE_CXX_EXTENSIONS"] = False
        toolchain.generate()

    def build(self):
        if not (Path(self.build_folder) / "CMakeCache.txt").is_file():
            raise ConanException(
                "Configure first: cmake -S . -B build -G Ninja"
            )
        CMake(self).build()

    def package(self):
        CMake(self).install()
