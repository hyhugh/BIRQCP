# Bootinfo and IRQ Control Provider

#### What is it
This is a boilerplate to use `capdl loader` to start child processes as `roottask`. So that you can have multiple capdl applications nested together.

#### How to run the demo
manifest: <https://github.com/hyhugh/BIRQCP-manifest>

0. init the repo by `repo init -u https://github.com/hyhugh/BIRQCP-manifest` followed by `repo sync`

1. adjust the capdl linker

   There is not much to adjust for capdl linker. Just simply disable the `use_large_frames` when `get_spec` from the elfs since the current capdl loader doesn't support mapping super frames.
   To disable `use_large_frames` you can add `use_large_frams=False` at line 58 of `capdl_linker.py` which is at `projects/camkes/capdl/cdl_utils/capdl_linker.py`.

2. build

    Create a build folder in the root directory and initialize the build in the new folder
    ```bash
    mkdir build
    cd build
    cmake -DCMAKE_TOOLCHAIN_FILE=../kernel/gcc.cmake -G Ninja -DTUTORIAL_DIR=BIRQCP -DTUT_BOARD=zynq7000 -DAARCH32=TRUE ..
    ```

    You possibly want to run the third command multiple times since SELFOUR-1951 is still a thing. When the project is not corerctly confiured, you could get strange errors like the real roottask capdl loader complaining about insufficient empty slots.

    Finally you build the project using `ninja` then you can run the demo using `./simulate` in the build directory.

#### Demo program structure

The demo program has the structure as below

<img src='https://g.gravizo.com/svg?
 digraph G {
    real_roottask -> fake_roottask;
    real_roottask -> BIRQCP;
    BIRQCP -> fake_roottask [label="provide bootinfo and IRQ Control"];
    fake_roottask -> timer;
    fake_roottask -> client;
 }'/>


`real_roottask` is the real roottask which is a `capdl loader`.


`fake_roottask` is a program running as roottask spawned by `real_roottask` which is also a `capdl loader`.


`BIRQCP` is the program which is used to setup the bootinfo of `fake_roottask` and handle the `IRQ Control` related syscalls for `fake_roottask`.

`timer` and `client` are grabbed from `sel4-tutorial`

#### Guide to use BIRQCP

To use BIRQCP you need to write an ad-hoc `manifest.py` for each of your `fake_roottask`s.
Please refer to the current `manifest.py`, I have written many comments inside there to explain.

Then you need to setup the build system, I have written a helper function called `CreateComponent` to conveniently create a `fake_roottask`, please refer to `CMakeLists.txt` for how to use it and how to setup the build system.
