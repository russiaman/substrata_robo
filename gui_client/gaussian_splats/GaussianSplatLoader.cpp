/*=====================================================================
GaussianSplatLoader.cpp
-------------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "GaussianSplatLoader.h"


#include "GaussianSplatZipReader.h"
#include "third_party/libwebp/src/webp/decode.h"
#include <maths/mathstypes.h>
#include <utils/Exception.h>
#include <utils/JSONParser.h>
#include <utils/Platform.h>
#include <cmath>


namespace
{


// Decodes a WebP image (from a map entry) to an RGBA8 buffer. Throws glare::Exception on failure.
struct DecodedImage
{
	DecodedImage() : data(NULL), w(0), h(0) {}
	~DecodedImage() { if(data) WebPFree(data); }
	GLARE_DISABLE_COPY(DecodedImage);

	uint8_t* data; // RGBA8, w * h * 4 bytes. Owned - freed with WebPFree().
	int w, h;
};


void decodeWebPEntry(const std::map<std::string, std::vector<uint8_t>>& entries, const std::string& filename, DecodedImage& image_out)
{
	const auto it = entries.find(filename);
	if(it == entries.end())
		throw glare::Exception("GaussianSplatLoader: SOG bundle is missing referenced file '" + filename + "'.");

	image_out.data = WebPDecodeRGBA(it->second.data(), it->second.size(), &image_out.w, &image_out.h);
	if(!image_out.data)
		throw glare::Exception("GaussianSplatLoader: failed to decode WebP image '" + filename + "'.");
}


std::vector<std::string> getChildStringArray(const JSONParser& parser, const JSONNode& node, const string_view name)
{
	const JSONNode& arr = node.getChildArray(parser, name);
	std::vector<std::string> res;
	res.reserve(arr.child_indices.size());
	for(uint32 idx : arr.child_indices)
		res.push_back(parser.nodes[idx].getStringValue());
	return res;
}


std::vector<double> getChildDoubleArray(const JSONParser& parser, const JSONNode& node, const string_view name)
{
	const JSONNode& arr = node.getChildArray(parser, name);
	std::vector<double> res(arr.child_indices.size());
	if(!res.empty())
		arr.parseDoubleArrayValues(parser, res.size(), res.data());
	return res;
}


float unlog(float n)
{
	const float sign = (n > 0.f) ? 1.f : ((n < 0.f) ? -1.f : 0.f);
	return sign * (std::exp(std::fabs(n)) - 1.f);
}


float lerp(double a, double b, float t) { return (float)(a + (b - a) * t); }


} // end anonymous namespace


GaussianSplatDataRef GaussianSplatLoader::loadFromBuffer(const uint8_t* data, size_t size)
{
	const std::map<std::string, std::vector<uint8_t>> entries = GaussianSplatZipReader::readEntries(data, size);

	const auto meta_it = entries.find("meta.json");
	if(meta_it == entries.end())
		throw glare::Exception("GaussianSplatLoader: SOG bundle is missing meta.json.");

	JSONParser json;
	json.parseBuffer((const char*)meta_it->second.data(), meta_it->second.size());
	const JSONNode& root = json.nodes[0];

	//----------------------------- means (positions) -----------------------------
	const JSONNode& means_node = root.getChildObject(json, "means");
	const std::vector<double> means_mins = getChildDoubleArray(json, means_node, "mins");
	const std::vector<double> means_maxs = getChildDoubleArray(json, means_node, "maxs");
	const std::vector<std::string> means_files = getChildStringArray(json, means_node, "files");
	if(means_mins.size() != 3 || means_maxs.size() != 3 || means_files.size() != 2)
		throw glare::Exception("GaussianSplatLoader: malformed 'means' entry in meta.json.");

	DecodedImage means_l, means_u;
	decodeWebPEntry(entries, means_files[0], means_l);
	decodeWebPEntry(entries, means_files[1], means_u);

	//----------------------------- scales -----------------------------
	const JSONNode& scales_node = root.getChildObject(json, "scales");
	const std::vector<double> scales_codebook = getChildDoubleArray(json, scales_node, "codebook");
	const std::vector<std::string> scales_files = getChildStringArray(json, scales_node, "files");
	if(scales_codebook.empty() || scales_files.size() != 1)
		throw glare::Exception("GaussianSplatLoader: malformed 'scales' entry in meta.json.");

	DecodedImage scales_img;
	decodeWebPEntry(entries, scales_files[0], scales_img);

	//----------------------------- quats -----------------------------
	const JSONNode& quats_node = root.getChildObject(json, "quats");
	const std::vector<std::string> quats_files = getChildStringArray(json, quats_node, "files");
	if(quats_files.size() != 1)
		throw glare::Exception("GaussianSplatLoader: malformed 'quats' entry in meta.json.");

	DecodedImage quats_img;
	decodeWebPEntry(entries, quats_files[0], quats_img);

	//----------------------------- sh0 (base colour + opacity) -----------------------------
	const JSONNode& sh0_node = root.getChildObject(json, "sh0");
	const std::vector<double> sh0_codebook = getChildDoubleArray(json, sh0_node, "codebook");
	const std::vector<std::string> sh0_files = getChildStringArray(json, sh0_node, "files");
	if(sh0_codebook.empty() || sh0_files.size() != 1)
		throw glare::Exception("GaussianSplatLoader: malformed 'sh0' entry in meta.json.");

	DecodedImage sh0_img;
	decodeWebPEntry(entries, sh0_files[0], sh0_img);

	//----------------------------- consistency checks -----------------------------
	const int W = means_l.w, H = means_l.h;
	if(means_u.w != W || means_u.h != H || scales_img.w != W || scales_img.h != H ||
		quats_img.w != W || quats_img.h != H || sh0_img.w != W || sh0_img.h != H)
		throw glare::Exception("GaussianSplatLoader: property image dimensions don't match between means/scales/quats/sh0.");

	const size_t num_pixels = (size_t)W * (size_t)H;
	const size_t count = root.hasChild("count") ? root.getChildUIntValue(json, "count") : num_pixels;
	if(count > num_pixels)
		throw glare::Exception("GaussianSplatLoader: meta.json 'count' exceeds available pixels.");

	//----------------------------- reconstruct splats -----------------------------
	const float SH_C0 = 0.28209479177387814f;

	GaussianSplatDataRef result = new GaussianSplatData();
	result->positions.resize(count);
	result->scales.resize(count);
	result->rotations.resize(count);
	result->colours.resize(count);

	js::AABBox aabb = js::AABBox::emptyAABBox();

	for(size_t i = 0; i < count; ++i)
	{
		const size_t px = i * 4;

		// Position: 16-bit quantised value split across means_l (low byte) and means_u (high byte) per channel.
		const uint32_t qx = ((uint32_t)means_u.data[px + 0] << 8) | means_l.data[px + 0];
		const uint32_t qy = ((uint32_t)means_u.data[px + 1] << 8) | means_l.data[px + 1];
		const uint32_t qz = ((uint32_t)means_u.data[px + 2] << 8) | means_l.data[px + 2];

		const float nx = lerp(means_mins[0], means_maxs[0], qx / 65535.f);
		const float ny = lerp(means_mins[1], means_maxs[1], qy / 65535.f);
		const float nz = lerp(means_mins[2], means_maxs[2], qz / 65535.f);

		const Vec3f pos(unlog(nx), unlog(ny), unlog(nz));
		result->positions[i] = pos;
		aabb.enlargeToHoldPoint(pos.toVec4fPoint());

		// Scale: 8-bit codebook index per axis, codebook holds log-domain values.
		result->scales[i] = Vec3f(
			std::exp((float)scales_codebook[scales_img.data[px + 0]]),
			std::exp((float)scales_codebook[scales_img.data[px + 1]]),
			std::exp((float)scales_codebook[scales_img.data[px + 2]]));

		// Rotation: "smallest three" quaternion encoding. R,G,B are the three stored components,
		// A (252..255) selects which of (w,x,y,z) was omitted and is reconstructed from the other three.
		const auto toComp = [](uint8_t c) -> float { return (c / 255.f - 0.5f) * 2.0f / 1.4142135623730951f; };
		const float a = toComp(quats_img.data[px + 0]);
		const float b = toComp(quats_img.data[px + 1]);
		const float c = toComp(quats_img.data[px + 2]);
		const int mode = quats_img.data[px + 3] - 252;
		const float t = a * a + b * b + c * c;
		const float d = std::sqrt(myMax(0.f, 1.f - t));
		float qw, qx_, qy_, qz_;
		switch(mode)
		{
		case 0: qw = d; qx_ = a; qy_ = b; qz_ = c; break; // omitted = w
		case 1: qw = a; qx_ = d; qy_ = b; qz_ = c; break; // omitted = x
		case 2: qw = a; qx_ = b; qy_ = d; qz_ = c; break; // omitted = y
		default: qw = a; qx_ = b; qy_ = c; qz_ = d; break; // omitted = z (mode == 3)
		}
		result->rotations[i] = Vec4f(qx_, qy_, qz_, qw); // Stored as (x, y, z, w).

		// Base colour (SH DC term) + opacity.
		const float r = 0.5f + (float)sh0_codebook[sh0_img.data[px + 0]] * SH_C0;
		const float g = 0.5f + (float)sh0_codebook[sh0_img.data[px + 1]] * SH_C0;
		const float bl = 0.5f + (float)sh0_codebook[sh0_img.data[px + 2]] * SH_C0;
		const float op = sh0_img.data[px + 3] / 255.f;
		result->colours[i] = Vec4f(r, g, bl, op);
	}

	result->aabb_os = aabb;

	return result;
}
