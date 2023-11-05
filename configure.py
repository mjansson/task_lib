#!/usr/bin/env python

"""Ninja build configurator for task library"""

import sys
import os
import glob
import shutil

sys.path.insert(0, os.path.join('build', 'ninja'))

import generator

dependlibs = ['task', 'foundation']

generator = generator.Generator(project='task', dependlibs=dependlibs, variables=[
                                ('bundleidentifier', 'com.maniccoder.task.$(binname)')])
target = generator.target
writer = generator.writer
toolchain = generator.toolchain

task_lib = generator.lib(module='task', sources=[
    'executor.c', 'fiber.c', 'scheduler.c', 'task.c', 'version.c'])

if generator.skip_tests():
  sys.exit()

includepaths = generator.test_includepaths()

extralibs = []

test_cases = [
    'task'
]
if toolchain.is_monolithic() or target.is_ios() or target.is_android() or target.is_tizen():
    # Build one fat binary with all test cases
    test_resources = []
    test_extrasources = []
    test_cases += ['all']
    if target.is_ios():
        test_resources = [os.path.join('all', 'ios', item) for item in [
            'test-all.plist', 'Images.xcassets', 'test-all.xib']]
        test_extrasources = [os.path.join('all', 'ios', 'viewcontroller.m')]
    elif target.is_android():
        test_resources = [os.path.join('all', 'android', item) for item in [
            'AndroidManifest.xml', os.path.join(
                'layout', 'main.xml'), os.path.join('values', 'strings.xml'),
            os.path.join('drawable-ldpi', 'icon.png'), os.path.join('drawable-mdpi',
                                                                    'icon.png'), os.path.join('drawable-hdpi', 'icon.png'),
            os.path.join('drawable-xhdpi', 'icon.png'), os.path.join('drawable-xxhdpi',
                                                                     'icon.png'), os.path.join('drawable-xxxhdpi', 'icon.png')
        ]]
        test_extrasources = [os.path.join('test', 'all', 'android', 'java', 'com', 'maniccoder', 'task', 'test', item) for item in [
            'TestActivity.java'
        ]]
    elif target.is_tizen():
        test_resources = [os.path.join('all', 'tizen', item) for item in [
            'tizen-manifest.xml', os.path.join('res', 'tizenapp.png')
        ]]
    dependlibs = ['test'] + dependlibs
    if target.is_macos() or target.is_ios() or target.is_android() or target.is_tizen():
        generator.app(module='', sources=[os.path.join(module, 'main.c') for module in test_cases] + test_extrasources, binname='test-task',
                      basepath='test', implicit_deps=[task_lib], libs=dependlibs, dependlibs=dependlibs, resources=test_resources, includepaths=includepaths)
    else:
        generator.bin(module='', sources=[os.path.join(module, 'main.c') for module in test_cases] + test_extrasources, binname='test-task',
                      basepath='test', implicit_deps=[task_lib], libs=dependlibs, dependlibs=dependlibs, resources=test_resources, includepaths=includepaths)
else:
    # Build one binary per test case
    if not generator.is_subninja:
        generator.bin(module='all', sources=['main.c'], binname='test-all', basepath='test',
                      implicit_deps=[task_lib], libs=dependlibs, dependlibs=dependlibs, includepaths=includepaths)
    dependlibs = ['test'] + dependlibs
    for test in test_cases:
        generator.bin(module=test, sources=['main.c'], binname='test-' + test, basepath='test', implicit_deps=[
                        task_lib], libs=dependlibs + extralibs, dependlibs=dependlibs, includepaths=includepaths)
