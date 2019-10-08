package=libzip
$(package)_version=1.5.2
$(package)_download_path=https://libzip.org/download/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=be694a4abb2ffe5ec02074146757c8b56084dbcebf329123c84b205417435e15
$(package)_dependencies=zlib

define $(package)_config_cmds
  ./configure --prefix=$(host_prefix) --with-zlib=$(host_prefix) --with-pic --enable-static --enable-shared=no
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  cp $($(package)_staging_dir)$(host_prefix)/lib/libzip/include/zipconf.h $($(package)_staging_dir)$(host_prefix)/include/zipconf.h
endef
