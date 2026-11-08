#include <libheif/heif.h>
#include <x265.h>
#include <libde265/de265.h>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

static void check(heif_error error, const char* operation) {
    if (error.code != heif_error_Ok)
        throw std::runtime_error(std::string(operation) + ": " + error.message);
}

static heif_error append(heif_context*, const void* data, size_t size, void* user) {
    auto& bytes = *static_cast<std::vector<uint8_t>*>(user);
    const auto* begin = static_cast<const uint8_t*>(data);
    bytes.insert(bytes.end(), begin, begin + size);
    return {heif_error_Ok, heif_suberror_Unspecified, "ok"};
}

static uint8_t sample(int plane, int x, int y) {
    return static_cast<uint8_t>(plane == 0 ? (x * 3 + y * 2) % 256
                                         : (80 + plane * 30 + x + y) % 256);
}

static void inspect_encoder_reconstruction() {
    const x265_api* api = x265_api_get(8);
    if (!api || api->sizeof_param != sizeof(x265_param))
        throw std::runtime_error("x265 header/library ABI mismatch");
    auto* param = api->param_alloc();
    api->param_default_preset(param, "slow", "ssim");
    api->param_apply_profile(param, "mainstillpicture");
    param->sourceWidth = param->sourceHeight = 64;
    param->fpsNum = param->fpsDenom = 1;
    param->totalFrames = 1;
    param->bLossless = 1;
    param->logLevel = X265_LOG_NONE;
    api->param_parse(param, "pools", "none");
    api->param_parse(param, "frame-threads", "1");
    param->bRepeatHeaders = 1;
    auto* encoder = api->encoder_open(param);
    if (!encoder) throw std::runtime_error("Cannot open diagnostic x265 encoder");
    x265_picture in{}, out{};
    api->picture_init(param, &in);
    api->picture_init(param, &out);
    std::vector<uint8_t> planes[3];
    for (int p = 0; p < 3; ++p) {
        int side = p == 0 ? 64 : 32;
        planes[p].resize(side * side);
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x)
            planes[p][y * side + x] = sample(p, x, y);
        in.planes[p] = planes[p].data();
        in.stride[p] = side;
    }
    x265_nal* nals = nullptr;
    uint32_t count = 0;
    int result = api->encoder_encode(encoder, &nals, &count, &in, &out);
    if (result == 0) result = api->encoder_encode(encoder, &nals, &count, nullptr, &out);
    if (result != 1 || !out.planes[0]) throw std::runtime_error("No x265 reconstruction");
    size_t differences = 0;
    for (int p = 0; p < 3; ++p) {
        int side = p == 0 ? 64 : 32;
        const auto* plane = static_cast<const uint8_t*>(out.planes[p]);
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x)
            differences += plane[y * out.stride[p] + x] != sample(p, x, y);
    }
    std::cerr << "Direct x265 reconstruction differences: " << differences << " / 6144\n";
    if (differences) throw std::runtime_error("Direct x265 reconstruction failed");
    auto* decoder = de265_new_decoder();
    if (de265_start_worker_threads(decoder, 1) != DE265_OK)
        throw std::runtime_error("Direct de265 worker failed");
    for (uint32_t i = 0; i < count; ++i) {
        if (de265_push_data(decoder, nals[i].payload, static_cast<int>(nals[i].sizeBytes), 0, nullptr) != DE265_OK)
            throw std::runtime_error("Direct de265 push failed");
    }
    if (de265_flush_data(decoder) != DE265_OK) throw std::runtime_error("Direct de265 flush failed");
    int more = 1;
    for (int i = 0; more && i < 100; ++i) {
        auto status = de265_decode(decoder, &more);
        if (status != DE265_OK) throw std::runtime_error(std::string("Direct de265: ") + de265_get_error_text(status));
    }
    const auto* decoded = de265_peek_next_picture(decoder);
    if (!decoded) throw std::runtime_error("Direct de265 has no picture");
    differences = 0;
    for (int p = 0; p < 3; ++p) {
        int stride = 0, side = p == 0 ? 64 : 32;
        const auto* plane = de265_get_image_plane(decoded, p, &stride);
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x)
            differences += plane[y * stride + x] != sample(p, x, y);
    }
    std::cerr << "Direct x265/de265 differences: " << differences << " / 6144\n";
    if (differences) throw std::runtime_error("Direct x265/de265 roundtrip failed");
    de265_free_decoder(decoder);
    api->encoder_close(encoder);
    api->param_free(param);
}

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("Expected output .heic path");
    inspect_encoder_reconstruction();
    check(heif_init(nullptr), "initialize");
    if (!heif_have_decoder_for_format(heif_compression_HEVC))
        throw std::runtime_error("HEVC decoder unavailable");
    using Context = std::unique_ptr<heif_context, decltype(&heif_context_free)>;
    using Image = std::unique_ptr<heif_image, decltype(&heif_image_release)>;
    using Handle = std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)>;
    using Encoder = std::unique_ptr<heif_encoder, decltype(&heif_encoder_release)>;
    {
        Context writeContext(heif_context_alloc(), heif_context_free);
        heif_encoder* rawEncoder = nullptr;
        check(heif_context_get_encoder_for_format(writeContext.get(), heif_compression_HEVC,
                                                &rawEncoder), "select HEVC encoder");
        Encoder encoder(rawEncoder, heif_encoder_release);
        check(heif_encoder_set_lossless(encoder.get(), 1), "set lossless");
        // Follow libheif's own heif_enc --lossless RGB path: GBR matrix and
        // 4:4:4 avoid an irreversible YCbCr420 -> RGB -> YCbCr420 conversion.
        check(heif_encoder_set_parameter(encoder.get(), "chroma", "444"), "lossless RGB chroma");
        check(heif_encoder_set_logging_level(encoder.get(), 3), "encoder diagnostics");
        check(heif_encoder_set_parameter(encoder.get(), "x265:pools", "none"), "limit x265 pools");
        check(heif_encoder_set_parameter(encoder.get(), "x265:frame-threads", "1"), "limit x265 threads");
        heif_image* rawImage = nullptr;
        check(heif_image_create(64, 64, heif_colorspace_RGB, heif_chroma_444, &rawImage), "create image");
        Image input(rawImage, heif_image_release);
        std::unique_ptr<heif_color_profile_nclx, decltype(&heif_nclx_color_profile_free)>
            profile(heif_nclx_color_profile_alloc(), heif_nclx_color_profile_free);
        check(heif_nclx_color_profile_set_color_primaries(profile.get(), 1), "color primaries");
        check(heif_nclx_color_profile_set_transfer_characteristics(profile.get(), 13), "transfer");
        check(heif_nclx_color_profile_set_matrix_coefficients(profile.get(), heif_matrix_coefficients_RGB_GBR), "RGB GBR matrix");
        profile->full_range_flag = 1;
        check(heif_image_set_nclx_color_profile(input.get(), profile.get()), "input color profile");
        const heif_channel channels[] = {heif_channel_R, heif_channel_G, heif_channel_B};
        for (int p = 0; p < 3; ++p) {
            const int side = 64;
            check(heif_image_add_plane(input.get(), channels[p], side, side, 8), "allocate plane");
            int stride = 0;
            auto* plane = heif_image_get_plane(input.get(), channels[p], &stride);
            if (!plane || stride < side) throw std::runtime_error("Invalid input plane");
            for (int y = 0; y < side; ++y)
                for (int x = 0; x < side; ++x) plane[y * stride + x] = sample(p, x, y);
        }
        std::unique_ptr<heif_encoding_options, decltype(&heif_encoding_options_free)>
            options(heif_encoding_options_alloc(), heif_encoding_options_free);
        options->output_nclx_profile = profile.get();
        heif_image_handle* rawEncoded = nullptr;
        check(heif_context_encode_image(writeContext.get(), input.get(), encoder.get(), options.get(),
                                       &rawEncoded), "encode HEVC");
        Handle encoded(rawEncoded, heif_image_handle_release);
        std::vector<uint8_t> bytes;
        heif_writer writer{1, append};
        check(heif_context_write(writeContext.get(), &writer, &bytes), "serialize HEIF");
        if (bytes.size() < 128) throw std::runtime_error("Unexpectedly small HEIF");
        Context readContext(heif_context_alloc(), heif_context_free);
        check(heif_context_read_from_memory_without_copy(readContext.get(), bytes.data(), bytes.size(),
                                                         nullptr), "read HEIF");
        heif_image_handle* rawPrimary = nullptr;
        check(heif_context_get_primary_image_handle(readContext.get(), &rawPrimary), "primary image");
        Handle primary(rawPrimary, heif_image_handle_release);
        if (heif_image_handle_get_width(primary.get()) != 64 ||
            heif_image_handle_get_height(primary.get()) != 64)
            throw std::runtime_error("Dimension roundtrip failed");
        heif_image* rawDecoded = nullptr;
        check(heif_decode_image(primary.get(), &rawDecoded, heif_colorspace_RGB, heif_chroma_444,
                                nullptr), "decode HEVC");
        Image decoded(rawDecoded, heif_image_release);
        size_t checked = 0, differences = 0;
        int maxDifference = 0;
        for (int p = 0; p < 3; ++p) {
            const int side = 64;
            int stride = 0;
            const auto* plane = heif_image_get_plane_readonly(decoded.get(), channels[p], &stride);
            if (!plane || stride < side) throw std::runtime_error("Invalid decoded plane");
            for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
                const int difference = std::abs(int(plane[y * stride + x]) - int(sample(p, x, y)));
                differences += difference != 0;
                maxDifference = std::max(maxDifference, difference);
                ++checked;
            }
        }
        Context invalid(heif_context_alloc(), heif_context_free);
        const char bad[] = "not a HEIF container";
        if (heif_context_read_from_memory_without_copy(invalid.get(), bad, sizeof(bad), nullptr).code
            == heif_error_Ok) throw std::runtime_error("Malformed input accepted");
        std::ofstream file(argv[1], std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) throw std::runtime_error("Cannot write test artifact");
        std::cout << "{\"ok\":" << (differences ? "false" : "true")
                  << ",\"direct_x265_reconstruction_equal\":true,\"direct_hevc_roundtrip_equal\":true,\"libheif\":\"" << heif_get_version()
                  << "\",\"encoder\":\"" << heif_encoder_get_name(encoder.get())
                  << "\",\"width\":64,\"height\":64,\"lossless_samples_checked\":" << checked
                  << ",\"lossless_colorspace\":\"RGB\",\"chroma\":\"444\",\"matrix_coefficients\":0"
                  << ",\"mismatched_samples\":" << differences << ",\"maximum_sample_error\":" << maxDifference
                  << ",\"heif_bytes\":" << bytes.size() << ",\"malformed_rejected\":true}" << std::endl;
        if (differences) throw std::runtime_error("HEIF lossless pixel comparison failed; artifact and metrics preserved");
    }
    heif_deinit();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
