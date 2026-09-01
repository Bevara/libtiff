/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / TIFF image decoder filter
 *  based on libtiff (http://www.libtiff.org/)
 *
 */

#include <gpac/filters.h>
#include <string.h>
#include "tiff_mem_io.h"

typedef struct
{
	GF_FilterPid *ipid, *opid;

	Bool is_playing;
	u32 src_timescale;
	u32 codec_id;
	u32 ofmt;
} GF_TIFFDecCtx;

static GF_Err tiffdec_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	const GF_PropertyValue *prop;
	GF_TIFFDecCtx *ctx = (GF_TIFFDecCtx *)gf_filter_get_udta(filter);

	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	prop = gf_filter_pid_get_property(pid, GF_PROP_PID_CODECID);
	if (!prop)
		return GF_NOT_SUPPORTED;
	ctx->ipid = pid;

	if (!ctx->opid)
	{
		ctx->opid = gf_filter_pid_new(filter);
	}

	// copy properties at init or reconfig
	gf_filter_pid_copy_properties(ctx->opid, ctx->ipid);
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(GF_CODECID_RAW));

	if (!ctx->ofmt)
	{
		ctx->ofmt = GF_PIXEL_RGB;
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_RGB));
	}
	else
	{
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(ctx->ofmt));
	}

	return GF_OK;
}

static GF_Err tiffdec_reconfigure_output(GF_Filter *filter, GF_FilterPid *pid)
{
	const GF_PropertyValue *p;
	GF_TIFFDecCtx *ctx = gf_filter_get_udta(filter);
	if (ctx->opid != pid)
		return GF_BAD_PARAM;

	p = gf_filter_pid_caps_query(pid, GF_PROP_PID_PIXFMT);
	if (p)
		ctx->ofmt = p->value.uint;
	return GF_OK;
}

static GF_Err tiffdec_process(GF_Filter *filter)
{
	GF_FilterPacket *pck, *dst_pck;
	u8 *data, *output;
	u32 size, width, height, out_size;
	TIFF *tif;
	TiffMemHandle handle;
	GF_TIFFDecCtx *ctx = (GF_TIFFDecCtx *)gf_filter_get_udta(filter);

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);

	if (!data)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_IO_ERR;
	}

	tif = tiffmem_open(&handle, data, size);
	if (!tif)
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[TIFF] TIFFClientOpen failed\n"));
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	width = height = 0;
	TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);

	if (!width || !height)
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[TIFF] invalid image dimensions\n"));
		TIFFClose(tif);
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	/* TIFFReadRGBAImageOriented only ever produces a packed R,G,B,A (little-endian uint32)
	 * raster - decode into a temporary RGBA buffer, then narrow to RGB if that is what
	 * was negotiated with the downstream filter (e.g. plain "out=rgb" with no encoder) */
	{
		uint32_t *rgba = (uint32_t *)gf_malloc(sizeof(uint32_t) * width * height);
		if (!rgba)
		{
			TIFFClose(tif);
			gf_filter_pid_drop_packet(ctx->ipid);
			return GF_OUT_OF_MEM;
		}

		if (!TIFFReadRGBAImageOriented(tif, width, height, rgba, ORIENTATION_TOPLEFT, 0))
		{
			GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[TIFF] TIFFReadRGBAImageOriented failed\n"));
			gf_free(rgba);
			TIFFClose(tif);
			gf_filter_pid_drop_packet(ctx->ipid);
			return GF_NON_COMPLIANT_BITSTREAM;
		}
		TIFFClose(tif);

		if (ctx->ofmt == GF_PIXEL_RGB)
		{
			u32 i;
			const u8 *src = (const u8 *)rgba;
			out_size = width * height * 3;

			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_RGB));
			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_WIDTH, &PROP_UINT(width));
			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_HEIGHT, &PROP_UINT(height));

			dst_pck = gf_filter_pck_new_alloc(ctx->opid, out_size, &output);
			if (!dst_pck)
			{
				gf_free(rgba);
				gf_filter_pid_drop_packet(ctx->ipid);
				return GF_OUT_OF_MEM;
			}
			for (i = 0; i < (u32)(width * height); i++)
			{
				output[i * 3] = src[i * 4];
				output[i * 3 + 1] = src[i * 4 + 1];
				output[i * 3 + 2] = src[i * 4 + 2];
			}
		}
		else
		{
			out_size = width * height * 4;

			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_RGBA));
			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_WIDTH, &PROP_UINT(width));
			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_HEIGHT, &PROP_UINT(height));

			dst_pck = gf_filter_pck_new_alloc(ctx->opid, out_size, &output);
			if (!dst_pck)
			{
				gf_free(rgba);
				gf_filter_pid_drop_packet(ctx->ipid);
				return GF_OUT_OF_MEM;
			}
			memcpy(output, rgba, out_size);
		}

		gf_free(rgba);
	}

	gf_filter_pck_merge_properties(pck, dst_pck);
	gf_filter_pck_set_dependency_flags(dst_pck, 0);
	gf_filter_pck_send(dst_pck);

	gf_filter_pid_drop_packet(ctx->ipid);

	return GF_OK;
}

static const GF_FilterCapability TIFFDecCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL),
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_CODECID, GF_4CC('T', 'I', 'F', 'F')),
		CAP_BOOL(GF_CAPS_INPUT_EXCLUDED, GF_PROP_PID_UNFRAMED, GF_TRUE),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
};

GF_FilterRegister TIFFDecoderRegister = {
	.name = "tiffdec",
	GF_FS_SET_DESCRIPTION("TIFF image decoder")
		GF_FS_SET_HELP("This filter decodes TIFF images using libtiff.")
			.private_size = sizeof(GF_TIFFDecCtx),
	SETCAPS(TIFFDecCaps),
	.configure_pid = tiffdec_configure_pid,
	.reconfigure_output = tiffdec_reconfigure_output,
	.process = tiffdec_process,
};

const GF_FilterRegister * EMSCRIPTEN_KEEPALIVE tiffdec_register(GF_FilterSession *session)
{
	return &TIFFDecoderRegister;
}


#include "filter_register.h"
__attribute__((constructor))
void register_tiffdec(void) {
    gf_filter_auto_register("tiffdec", tiffdec_register);
}
