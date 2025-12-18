#include <iostream>
#include <string>
#include <tiffio.h> // For libtiff
#include <png.h>    // For libpng
#include <stdexcept>
#include <cstdlib>

//A type to manage resources and ensure proper cleanup
struct Resources
{
    uint32_t *raster = nullptr;
    FILE *fp = nullptr;
    png_structp png_ptr = nullptr;
    png_infop info_ptr = nullptr;
    png_bytep row = nullptr;

    ~Resources()
    {
        if (row)
            free(row);
        if (png_ptr)
            png_destroy_write_struct(&png_ptr, info_ptr ? &info_ptr : (png_infopp)NULL);
        if (fp)
            fclose(fp);
        if (raster)
            _TIFFfree(raster);
    }
};

// Function to convert a TIFF image to PNG format
static void save_tiff_as_png(TIFF *tif, const char *png_filename)
{
    if (!tif || !png_filename)
        throw std::invalid_argument("Invalid arguments to save_tiff_as_png");

    uint32_t width = 0, height = 0;

    if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width) || !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height))
        throw std::invalid_argument("Failed to get image dimensions");

    // Resources to manage
    Resources res{};

    // Allocate raster for RGBA pixels
    tsize_t npixels = (tsize_t)width * (tsize_t)height;

    res.raster = (uint32_t *)_TIFFmalloc(npixels * sizeof(uint32_t));

    if (!res.raster)
        throw std::runtime_error("Failed to allocate raster");

    // Read into raster in top-left orientation
    if (!TIFFReadRGBAImageOriented(tif, width, height, res.raster, ORIENTATION_TOPLEFT, 0))
        throw std::runtime_error("TIFFReadRGBAImageOriented failed");

    // Initialize libpng structures
    res.fp = fopen(png_filename, "wb");
    if (!res.fp)
        throw std::runtime_error("Failed to open output PNG file");

    res.png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!res.png_ptr)
        throw std::runtime_error("png_create_write_struct failed");

    res.info_ptr = png_create_info_struct(res.png_ptr);
    if (!res.info_ptr)
        throw std::runtime_error("png_create_info_struct failed");

    if (setjmp(png_jmpbuf(res.png_ptr)))
        throw std::runtime_error("libpng internal processing error");

    png_init_io(res.png_ptr, res.fp);

    // We will write RGBA 8-bit
    png_set_IHDR(res.png_ptr, res.info_ptr, width, height,
                 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(res.png_ptr, res.info_ptr);

    // Allocate a single row buffer for RGBA with 1 byte for each color channel
    res.row = (png_bytep) malloc((size_t)width * 4 * 1);

    if (!res.row)
        throw std::runtime_error("Failed to allocate row buffer");

    for (uint32_t y = 0; y < height; ++y)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            uint32_t px = res.raster[(size_t)y * width + x];

            // TIFFReadRGBAImage returns data in host byte order as 0xAARRGGBB on most systems.
            res.row[x * 4 + 0] = (uint8_t)((px >> 16) & 0xFF); // R
            res.row[x * 4 + 1] = (uint8_t)((px >> 8) & 0xFF);  // G
            res.row[x * 4 + 2] = (uint8_t)(px & 0xFF);         // B
            res.row[x * 4 + 3] = (uint8_t)((px >> 24) & 0xFF); // A
        }

        png_write_row(res.png_ptr, res.row);
    }

    png_write_end(res.png_ptr, res.info_ptr);
}

int main(int argc, char *argv[])
{
    // Get the TIFF file name from argument
    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " <tiff_file>" << std::endl;

        return 1;
    }

    const char *tiff_file = argv[1];

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

    int result = 0;

    try {
        save_tiff_as_png(tif, output_file.c_str());
    }
    catch (const std::exception &e)
    {
        std::cout << "Failed to convert TIFF to PNG: " << e.what() << std::endl;

        result = 1;
    }

    TIFFClose(tif);

    return result;
}