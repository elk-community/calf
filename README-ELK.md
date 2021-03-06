# Calf

Modified for headless plugin build for [Elk Audio OS](https://elk.audio)

## Building Instructions

### Generate ttl files
This is done first since the script to generate the `.ttl` files won't run with the cross compilation toolchain.
1. Make a folder to put the native build in `mkdir native`.

2. Generate project files:

   ```bash
   $ ./autogen.sh --prefix=/absoute/path/to/calf/native
   ````

3. `make`and `make install` to generate the ttl files.

4. `make clean` to prepare for cross compilation.

### Cross Compile the plugins
1. Set up the cross-compilation toolchain:

   ```bash
   $ unset LD_LIBRARY_PATH
   $ source /path/to/environment-setup-cortexa7t2hf-neon-vfpv4-elk-linux-gnueabi
   ```

2. Create a directory to build and install the cross compilation files to `$ mkdir build`.

3. Generate the makefiles:

    ```bash
    # for raspberry pi 3
    $ ./configure --host=arm-elk-linux-gnu --prefix=/absolute/path/to/calf/build
    # for raspberry pi 4
    $ ./configure --host=aarch64-elk-linux-gnu --prefix=/absolute/path/to/calf/build
    ```

4. Compile the plugin library:
    ```bash
    $ make
    $ make install
    ```

5. move the .ttl files to the cross compiled build.

    ```bash
    $ cp native/lib/lv2/calf.lv2/*.ttl build/lib/lv2/calf.lv2/
    ```

6. move the folder build/lib/lv2/calf.lv2 to the desired location on the Elk Pi. Make sure the path to the parent folder is in the `LV2_PATH` variable.

## Additional notes

* For further compilation help. Look at our [documentation](https://github.com/elk-audio/elk-docs/blob/master/documents/building_plugins_for_elk.md).
