/*
    $Id$

    Copyright (C) 2001 Herbert Valerio Riedel <hvr@gnu.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <libvcd/vcd.h>
#include <libvcd/vcd_assert.h>
#include <libvcd/vcd_bytesex.h>
#include <libvcd/vcd_cd_sector.h>
#include <libvcd/vcd_image_cdrdao.h>
#include <libvcd/vcd_iso9660.h>
#include <libvcd/vcd_logging.h>
#include <libvcd/vcd_stream_stdio.h>
#include <libvcd/vcd_util.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char _rcsid[] = "$Id$";

/* reader */

/****************************************************************************
 * writer
 */

typedef struct {
  bool sector_2336_flag;
  char *toc_fname;
  char *img_base;

  VcdDataSink *last_bin_snk;
  int last_snk_idx;
  bool last_pause;

  VcdList *vcd_cue_list;
} _img_cdrdao_snk_t;

static void
_sink_free (void *user_data)
{
  _img_cdrdao_snk_t *_obj = user_data;

  /* fixme -- destroy cue list */

  vcd_data_sink_destroy (_obj->last_bin_snk);
  free (_obj->toc_fname);
  free (_obj->img_base);
  free (_obj);
}

static int
_set_cuesheet (void *user_data, const VcdList *vcd_cue_list)
{
  _img_cdrdao_snk_t *_obj = user_data;
  VcdDataSink *toc_snk = vcd_data_sink_new_stdio (_obj->toc_fname);

  VcdListNode *node;

  int track_no, index_no;
  const vcd_cue_t *_last_cue = 0;
  unsigned last_track_lsn = 0;

  vcd_data_sink_printf (toc_snk, 
			"// CDRDAO TOC\n"
			"//  generated by %s\n\n"
			"CD_ROM_XA\n", vcd_version_string (false));

  _obj->vcd_cue_list = _vcd_list_new ();

  index_no = track_no = 0;
  _VCD_LIST_FOREACH (node, (VcdList *) vcd_cue_list)
    {
      const vcd_cue_t *_cue = _vcd_list_node_data (node);

      /* copy cue list while traversing */
      {
	vcd_cue_t *_cue2 = _vcd_malloc (sizeof (vcd_cue_t));
	*_cue2 = *_cue;
	_vcd_list_append (_obj->vcd_cue_list, _cue2);
      }
      
      switch (_cue->type)
	{
	case VCD_CUE_TRACK_START:
	  track_no++;
	  index_no = 0;

	  last_track_lsn = _cue->lsn;

	  vcd_data_sink_printf (toc_snk, 
				"\n// Track %d\n"
				"TRACK %s\n COPY\n",
				track_no,
				(_obj->sector_2336_flag ? "MODE2_FORM_MIX" : "MODE2_RAW"));

	  if (_last_cue && _last_cue->type == VCD_CUE_PREGAP_START)
	    vcd_data_sink_printf (toc_snk, 
				  " DATAFILE \"%s_%.2d_pregap.img\"\n"
				  " START\n",
				  _obj->img_base, track_no);

	  index_no++;

	  vcd_data_sink_printf (toc_snk, 
				" DATAFILE \"%s_%.2d.img\"\n",
				_obj->img_base, track_no);
	  break;

	case VCD_CUE_PREGAP_START:
	  /* handled in next iteration */
	  break;

	case VCD_CUE_SUBINDEX:
	  index_no++;

	  {
	    msf_t _msf = { 0, 0, 0 };

	    lba_to_msf (_cue->lsn - last_track_lsn, &_msf);

	    vcd_data_sink_printf (toc_snk, 
				  " INDEX %2.2x:%2.2x:%2.2x\n",
				  _msf.m, _msf.s, _msf.f);
	  }
	  break;

	case VCD_CUE_END:
	  vcd_data_sink_printf (toc_snk, "\n// EOF\n");

	  vcd_data_sink_close (toc_snk);
	  vcd_data_sink_destroy (toc_snk);

	  return 0;
	  break;
	}

      _last_cue = _cue;
    }

  vcd_assert_not_reached ();

  return -1;
}
 
static int
_write (void *user_data, const void *data, uint32_t lsn)
{
  const char *buf = data;
  _img_cdrdao_snk_t *_obj = user_data;
  long offset;

  {
    VcdListNode *node;
    uint32_t _last = 0;
    uint32_t _ofs = 0;
    bool _lpregap = false;
    bool _pregap = false;

    int num = 0, in_track = 0;
    _VCD_LIST_FOREACH (node, _obj->vcd_cue_list)
      {
	const vcd_cue_t *_cue = _vcd_list_node_data (node);
    
	switch (_cue->type) 
	  {
	  case VCD_CUE_PREGAP_START:
	  case VCD_CUE_END:
	  case VCD_CUE_TRACK_START:
	    if (_cue->lsn && IN (lsn, _last, _cue->lsn - 1))
	      {
		vcd_assert (in_track == 0);
		in_track = num;
		_ofs = _last;
		_pregap = _lpregap;
	      }

	    _last = _cue->lsn;  
	    _lpregap = (_cue->type == VCD_CUE_PREGAP_START);

	    if (_cue->type == VCD_CUE_TRACK_START)
	      num++;
	    break;

	  default:
	    /* noop */
	    break;
	  }
      }

    vcd_assert (in_track != 0);
    vcd_assert (_obj->last_snk_idx <= in_track);

    if (_obj->last_snk_idx != in_track
	|| _obj->last_pause != _pregap)
      {
	char buf[4096] = { 0, };

	if (_obj->last_bin_snk)
	  vcd_data_sink_destroy (_obj->last_bin_snk);

	snprintf (buf, sizeof (buf),
		  "%s_%.2d%s.img",
		  _obj->img_base,
		  (_pregap ? in_track + 1 : in_track), 
		  (_pregap ? "_pregap" : ""));
	
	_obj->last_bin_snk = vcd_data_sink_new_stdio (buf);
	_obj->last_snk_idx = in_track;
	_obj->last_pause = _pregap;
      }

    vcd_assert (lsn >= _ofs);
    offset = lsn - _ofs;
  }

  offset *= _obj->sector_2336_flag ? M2RAW_SIZE : CDDA_SIZE;

  vcd_data_sink_seek(_obj->last_bin_snk, offset);
  
  if (_obj->sector_2336_flag)
    vcd_data_sink_write(_obj->last_bin_snk, buf + 12 + 4, M2RAW_SIZE, 1);
  else
    vcd_data_sink_write(_obj->last_bin_snk, buf, CDDA_SIZE, 1);
  
  return 0;
}

VcdImageSink *
vcd_image_sink_new_cdrdao (const char toc_fname[],
			   const char img_basename[],
			   bool sector_2336)
{
  _img_cdrdao_snk_t *_data;
  
  vcd_image_sink_funcs _funcs = {
    set_cuesheet: _set_cuesheet,
    write: _write,
    free: _sink_free
  };

  if (!toc_fname || !img_basename)
    return NULL;

  _data = _vcd_malloc (sizeof (_img_cdrdao_snk_t));

  _data->toc_fname = strdup (toc_fname);
  _data->img_base = strdup (img_basename);

  _data->sector_2336_flag = sector_2336;

  return vcd_image_sink_new (_data, &_funcs);
}

