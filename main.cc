#include <iostream>
#include <string>
#include <tiffio.h> // For libtiff
#include <png.h>    // For libpng
#include <stdexcept>
#include <cstdlib>

//A type to manage resources and ensure proper cleanup
struct Resources
{
    FILE *fp = nullptr;
    png_structp png_ptr = nullptr;
    png_infop info_ptr = nullptr;
    tdata_t row = nullptr;

    ~Resources()
    {
        if (row)
            _TIFFfree(row);
        if (png_ptr)
            png_destroy_write_struct(&png_ptr, info_ptr ? &info_ptr : (png_infopp)NULL);
        if (fp)
            fclose(fp);
    }
};

// Function to convert a TIFF image to PNG format
static void save_tiff_as_png(TIFF *tif, const char *png_filename)
{
    if (!tif || !png_filename)
        throw std::invalid_argument("Invalid arguments to save_tiff_as_png");

    uint32_t width = 0, height = 0, bps = 0, spp = 0, photometric = 0;

    if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width) || 
        !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height) ||
        !TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bps) ||
        !TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &spp) ||
        !TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &photometric))
        throw std::invalid_argument("Failed to get image properties from TIFF file");

    // Determine PNG Color Type
    int png_color_type;

    if (photometric == PHOTOMETRIC_MINISBLACK) {
        png_color_type = (spp == 2) ? PNG_COLOR_TYPE_GRAY_ALPHA : PNG_COLOR_TYPE_GRAY;
    } else if (photometric == PHOTOMETRIC_RGB) {
        png_color_type = (spp == 4) ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB;
    } else {
        throw std::invalid_argument("Unsupported photometric interpretation\n");
    }

    // Resources to manage
    Resources res{};

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
                 bps, png_color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_write_info(res.png_ptr, res.info_ptr);

    // If it's a 16-bit TIFF, ensure we handle endianness (PNG is big-endian)
    if (bps == 16) {
        #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            //This will cause png_write_row to swap bytes from little-endian to big-endian
            png_set_swap(res.png_ptr);
        #endif
    }

    // Dynamic Buffer: Allocate exactly what a single scanline needs
    tmsize_t line_size = TIFFScanlineSize(tif);
    res.row = _TIFFmalloc(line_size);

    if (!res.row)
        throw std::runtime_error("Failed to allocate row buffer");

    // Read and Write row by row
    for (uint32_t row = 0; row < height; row++) {
        //This will give us the pixel values in machine's endinaness
        TIFFReadScanline(tif, res.row, row, 0);
        //We supply the pixel values in machine's endinaness.
        //png_write_row will convert the values to big endian if necessary
        png_write_row(res.png_ptr, (png_bytep) res.row);
    }

    png_write_end(res.png_ptr, res.info_ptr);
}

bool convert_file(const char *tiff_file)
{
    TIFF *tif = TIFFOpen(tiff_file, "r");

    if (!tif)
    {
        std::cout << "Error: Could not open TIFF file" << std::endl;

        return false;
    }

    // Replace the extension of the TIFF file name with .png for output
    std::string output_file = std::string(tiff_file);
    size_t dot_pos = output_file.find_last_of('.');

    if (dot_pos != std::string::npos)
        output_file = output_file.substr(0, dot_pos) + ".png";
    else
        output_file += ".png";

    bool result = false;

    try {
        save_tiff_as_png(tif, output_file.c_str());

        result = true;
    }
    catch (const std::exception &e)
    {
        std::cout << "Failed to convert TIFF to PNG: " << e.what() << std::endl;
    }

    TIFFClose(tif);

    return result;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " TIFF_FILE1 TIFF_FILE2 ..." << std::endl;

        return 1;
    }

    int exit_code = 0;

    for (int i = 1; i < argc; ++i)
    {
        if (!convert_file(argv[i]))
        {
            std::cerr << "Failed to convert: " << argv[i] << std::endl;

            exit_code = 1;
        }
    }

    return exit_code;
}