#include <iostream>
#include <vector>
#include <string>
#include <tiffio.h> // For libtiff
#include <png.h>    // For libpng
#include <stdexcept>
#include <cstdlib>

static bool save_tiff_as_png(TIFF* tif, const char* png_filename)
{
    if (!tif || !png_filename)
        return false;

    uint32_t width = 0, height = 0;

    if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width) || !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height))
        return false;

    // Resources to manage
    uint32_t* raster = nullptr;
    FILE* fp = nullptr;
    png_structp png_ptr = nullptr;
    png_infop info_ptr = nullptr;
    std::vector<png_bytep> row_pointers;

    try
    {
        // Allocate raster for RGBA pixels
        tsize_t npixels = (tsize_t)width * (tsize_t)height;
        raster = (uint32_t*) _TIFFmalloc(npixels * sizeof(uint32_t));

        if (!raster)
            throw std::runtime_error("Failed to allocate raster");

        // Read into raster in top-left orientation
        if (!TIFFReadRGBAImageOriented(tif, width, height, raster, ORIENTATION_TOPLEFT, 0))
            throw std::runtime_error("TIFFReadRGBAImageOriented failed");

        // Initialize libpng structures
        fp = fopen(png_filename, "wb");

        if (!fp)
            throw std::runtime_error("Failed to open output PNG file");

        png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

        if (!png_ptr)
            throw std::runtime_error("png_create_write_struct failed");

        info_ptr = png_create_info_struct(png_ptr);

        if (!info_ptr)
            throw std::runtime_error("png_create_info_struct failed");

        if (setjmp(png_jmpbuf(png_ptr)))
            throw std::runtime_error("libpng internal processing error");

        png_init_io(png_ptr, fp);

        // We will write RGBA 8-bit
        png_set_IHDR(png_ptr, info_ptr, width, height,
                     8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        png_write_info(png_ptr, info_ptr);

        // Create row buffer and write rows
        row_pointers.resize(height);

        for (uint32_t y = 0; y < height; ++y)
        {
            png_bytep row = (png_bytep)malloc((size_t)width * 4);
            if (!row)
                throw std::runtime_error("Failed to allocate row buffer");

            for (uint32_t x = 0; x < width; ++x)
            {
                uint32_t px = raster[(size_t)y * width + x];

                // TIFFReadRGBAImage returns data in host byte order as 0xAARRGGBB on most systems.
                uint8_t r = (uint8_t)((px >> 16) & 0xFF);
                uint8_t g = (uint8_t)((px >> 8) & 0xFF);
                uint8_t b = (uint8_t)(px & 0xFF);
                uint8_t a = (uint8_t)((px >> 24) & 0xFF);

                png_bytep ptr = row + x * 4;
                ptr[0] = r;
                ptr[1] = g;
                ptr[2] = b;
                ptr[3] = a;
            }

            row_pointers[y] = row;
        }

        png_write_image(png_ptr, row_pointers.data());
        png_write_end(png_ptr, info_ptr);

        // Normal cleanup
        for (uint32_t y = 0; y < height; ++y)
            free(row_pointers[y]);

        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        _TIFFfree(raster);

        return true;
    }
    catch (...)
    {
        // Unified cleanup on error
        for (size_t y = 0; y < row_pointers.size(); ++y)
            if (row_pointers[y]) free(row_pointers[y]);

        if (png_ptr)
            png_destroy_write_struct(&png_ptr, info_ptr ? &info_ptr : (png_infopp)NULL);

        if (fp) fclose(fp);
        if (raster) _TIFFfree(raster);

        return false;
    }
}

int main(int argc, char *argv[])
{
    // Get the TIFF file name from argument
    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " <tiff_file>" << std::endl;
        return 1;
    }

    const char* tiff_file = argv[1];

    // 1. Open TIFF file
    TIFF *tif = TIFFOpen(tiff_file, "r");

    if (!tif)
    {
        std::cout << "Error: Could not open TIFF file" << std::endl;
        return 1;
    }

    // Replace the extension of the TIFF file name with .png for output
    std::string output_file = std::string(tiff_file);
    size_t dot_pos = output_file.find_last_of('.');

    if (dot_pos != std::string::npos)
        output_file = output_file.substr(0, dot_pos) + ".png";
    else
        output_file += ".png";

    bool ok = save_tiff_as_png(tif, output_file.c_str());

    TIFFClose(tif);

    if (!ok)
    {
        std::cout << "Failed to convert TIFF to PNG: " << output_file << std::endl;
        return 1;
    }

    std::cout << "Wrote PNG: " << output_file << std::endl;
    return 0;
}