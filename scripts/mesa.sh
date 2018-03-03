make clean
rm -rf .pc
rm -f config.cache config.log config.status
rm -f */config.cache */config.log */config.status
rm -f conftest* */conftest*
rm -rf autom4te.cache */autom4te.cache
rm -rf build
rm -rf configure bin/config.guess bin/config.sub config.h.in
rm -rf $(find -name Makefile.in)
rm -rf aclocal.m4 bin/missing bin/depcomp install-sh bin/ltmain.sh
rm -f bin/ar-lib bin/compile bin/ylwrap bin/install-sh
rm -rf debian/stamp
for file in debian/*.in; do rm -f ${file%%.in}; done
rm -f src/glsl/builtins/tools/texture_builtins.pyc
rm -f src/mapi/glapi/gen/*.pyc
rm -f src/mesa/main/*.pyc
rm -f src/gallium/auxiliary/util/*.pyc
rm -f m4/libtool.m4 m4/ltoptions.m4 m4/ltsugar.m4 m4/ltversion.m4 m4/lt~obsolete.m4
rm -rf *.lo
rm -rf *.la
rm -rf *.a
rm -rf *.o

echo "18.1.0-dev-ben-$(date)" > VERSION

./autogen.sh \
--prefix=/usr/local \
--sysconfdir=\${prefix}/etc \
--libdir=\${prefix}/lib/ \
--localstatedir=/var \
--build=x86_64-linux-gnu \
--host=x86_64-linux-gnu \
--enable-dri \
--with-dri-drivers=" nouveau i915 i965 r200 radeon swrast" \
--with-dri-driverdir=\${prefix}/lib/dri \
--with-dri-searchpath='/usr/local/lib/dri:\$${ORIGIN}/dri:/usr/lib/dri' \
--enable-osmesa \
--enable-glx-tls \
--enable-shared-glapi \
--enable-texture-float \
--enable-driglx-direct \
--enable-dri3 \
--with-platforms="x11 wayland drm" \
--enable-xa \
--enable-llvm ac_cv_path_LLVM_CONFIG=llvm-config-6.0 \
--enable-vdpau \
--enable-omx-bellagio \
--enable-va \
--enable-xvmc \
--enable-opencl \
--enable-opencl-icd \
--enable-nine \
--enable-gallium-extra-hud \
--enable-lmsensors \
--with-gallium-drivers=" nouveau svga r600 r300 i915 virgl radeonsi swrast" \
--enable-gles1 \
--enable-gles2 \
--enable-gle \
--with-vulkan-drivers=intel,radeon \
CFLAGS="-O3 -fstack-protector-strong -Wall -Wextra -Werror=format-security -fno-omit-frame-pointer" \
CPPFLAGS="-Wall -Wextra -D_FORTIFY_SOURCE=2" \
CXXFLAGS="-O3 -fstack-protector-strong -Wall -Wextra -Werror=format-security -fno-omit-frame-pointer" \
FCFLAGS="-O3 -fstack-protector-strong" \
FFLAGS="-O3 -fstack-protector-strong" \
GCJFLAGS="-O3 -fstack-protector-strong" \
LDFLAGS="-Bsymbolic-functions -z relro" \
OBJCFLAGS="-O3 -fstack-protector-strong -Wall -Wextra -Werror=format-security" \
OBJCXXFLAGS="-O3 -fstack-protector-strong -Wall -Wextra -Werror=format-security" \
CC=gcc \
CXX=g++

make -j16 || exit 125

sudo make install
sudo ldconfig -v
rm -rf ~/.cache/mesa_shader_cache/
