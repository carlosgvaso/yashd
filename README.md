yashd
=====

Yet Another SHell daemon (yashd) is a daemon service that provides a basic Linux
shell built on C with minimum library dependencies and a client to talk to the
service.

Authors:

 * Jose Carlos Martinez Garcia-Vaso <carlosgvaso@utexas.edu>
 * Utkarsh Vardan <uvardan@utexas.edu>


Compiling
---------

To compile the full project, run the following command from the root project
folder `yashd`:

```console
make
```

Other compiling options are:

 * `make debug`: To compile with GDB symbols enabled.
 
 * `make yashd`: To compile the yashd server daemon only.
 
 * `make yash`: To compile the yash client only.


Usage
-----


### Yashd server daemon

```console
Usage:
./yashd [options]

Options:
    -h, --help              Print help and exit
    -p PORT, --port PORT    Server port [1024-65535]
    -v, --verbose           Verbose logger output
```


### Yash client

```console
Usage:
./yash [options] <host>

Required arguments:
    host                    Yashd server host address

Options:
    -h, --help              Print help and exit
    -p PORT, --port PORT    Yashd server port [1024-65535]
```


Documentation
-------------

You can generate detailed documentation using Doxygen. Running the following
command from the project's root directory will generate the documentation
inside the `/doc/` directory:

```console
doxygen yashd.doxyfile
```
