kmssink_sources = [
  'gstkmsallocator.c',
  'gstkmsbufferpool.c',
  'gstkmssink.c',
  'gstkmsutils.c',
]

if host_system != 'linux'
  subdir_done()
endif

libdrm_dep = dependency('libdrm', version : '>= 2.4.98',
                        required : get_option('kms'),
                        fallback: ['libdrm', 'ext_libdrm'])
if libdrm_dep.found()
kms_args = gst_plugins_bad_args
if libdrm_dep.type_name() == 'internal'
	inc_drm = subproject('libdrm').get_variable('inc_drm')
	have_hdr = cc.has_type('struct hdr_output_metadata', prefix : '#include <drm_mode.h>', include_directories : inc_drm)
	have_gen_hdr = cc.has_type('struct _gen_hdr_output_metadata', prefix : '#include <drm_mode.h>', include_directories : inc_drm)
else
	have_hdr = cc.has_type('struct hdr_output_metadata', prefix : '#include <drm_mode.h>', dependencies: [libdrm_dep])
	have_gen_hdr = cc.has_type('struct gen_hdr_output_metadata', prefix : '#include <drm_mode.h>', dependencies: [libdrm_dep])
endif

if have_hdr
  kms_args += ['-DHAVE_HDR_OUTPUT_METADATA=1']
endif
if have_gen_hdr
  kms_args += ['-DHAVE_GEN_HDR_OUTPUT_METADATA=1']
endif

  gstkmssink = library('gstkms',
    kmssink_sources,
    c_args : kms_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstvideo_dep, gstallocators_dep, libdrm_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstkmssink, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstkmssink]
endif
