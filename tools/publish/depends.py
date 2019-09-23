#!/usr/bin/env python3

#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright © 2011-2019 ANSSI. All Rights Reserved.
#
# Author(s): fabienfl
#

import argparse
import os
import shutil
import subprocess


def parse():
    parser = argparse.ArgumentParser()
    parser.add_argument("--vcpkg-root", help="vcpkg root directory")
    parser.add_argument("--extract-copyright", help="extract copyright in a directory")
    parser.add_argument("package", help="package name")
    return parser.parse_args()


def get_installed_packages(vcpkg_root):
    vcpkg = os.path.join(vcpkg_root, "vcpkg.exe")
    process = subprocess.run([vcpkg, "list"], capture_output=True)

    stdout = process.stdout.decode('utf-8')

    package_list = set()
    for line in stdout.split("\n"):
        if "-windows-" in line:
            package_list.add(line.split(":")[0])

    return package_list


def build_dependency_tree(package_name, flat_dependencies):

    if not flat_dependencies[package_name]:
        return None

    tree = {}
    for dep in flat_dependencies[package_name]:
        tree[dep] = build_dependency_tree(dep, flat_dependencies)

    return tree


def get_package_dependencies(vcpkg_root, package_name):
    vcpkg = os.path.join(vcpkg_root, "vcpkg.exe")

    process = subprocess.run(
        [vcpkg, "depend-info", package_name], capture_output=True)

    stdout = process.stdout.decode('utf-8')

    flat_dependencies = {}
    deps = set()
    for line in stdout.split("\r\n"):
        if not ":" in line:
            continue

        name = line.split(":")[0]

        deps = line.split(":")[1].split(",")
        deps = [x.strip(' ') for x in deps]
        deps = [x for x in deps if x]

        flat_dependencies[name] = deps

    return build_dependency_tree(package_name, flat_dependencies)


def walk_dependencies(func, package_name, packages, level=0, seen=set()):
    if not package_name or package_name in seen:
        return

    seen.add(package_name)

    func(package_name, level)

    deps = packages.get(package_name)
    if not deps:
        return

    for child in deps.keys():
        walk_dependencies(func, child, packages, level+1, seen)


def main():
    args = parse()

    package_list = get_installed_packages(args.vcpkg_root)

    packages = {}
    for package_name in package_list:
        deps = get_package_dependencies(args.vcpkg_root, package_name)

        packages[package_name] = deps

    if args.package:
        def printPackage(name, level):
            print("    " * level + name)

        walk_dependencies(printPackage, args.package, packages)

        def printCopyrightPath(name, level):
            triplets = [
                "x86-windows",
                "x64-windows",
                "x86-windows-static",
                "x64-windows-static"
            ]

            found = False
            for triplet in triplets:
                path = os.path.join(
                    args.vcpkg_root,
                    "installed",
                    triplet,
                    "share",
                    name,
                    "copyright")

                if os.path.exists(path):
                    found = True
                    break

            if not found:
                print("NO COPYRIGHT FOUND FOR '{}'".format(name))
                return

            print(path)

            if args.extract_copyright:
                dest = os.path.join(args.extract_copyright, name)
                os.makedirs(dest, exist_ok=True)
                shutil.copy(path, dest)

        if not args.package in packages:
            print("'{}' is not installed".format(args.package))
            return 1

        walk_dependencies(printCopyrightPath, args.package, packages, seen=set())


if __name__ == '__main__':
    main()
