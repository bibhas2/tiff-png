This utility converts a TIFF file into a PNG file.

## Building in Ubuntu

Install Gnu C++ compiler.

```
sudo apt install build-essential
```

Install the required packages.

```
sudo apt update

sudo apt install libtiff-dev libwebp-dev libzstd-dev zlib1g-dev

sudo apt install libpng-dev
```

Compile the code.

```
g++ -o tiff-png main.cc -ltiff -lpng
```

