{ pkgs ? import <nixpkgs> {} }:
let
  boostBoth = pkgs.boost181.overrideAttrs (oldAttrs: {
    configurePhase = ''
      ./bootstrap.sh --prefix=$out
    '';
    
buildPhase = ''
  # Build both shared and static libraries WITH PIC
  ./b2 -j$NIX_BUILD_CORES \
    variant=release \
    link=shared,static \
    threading=multi \
    runtime-link=shared \
    cxxflags=-fPIC \
    cflags=-fPIC \
    ${pkgs.lib.concatStringsSep " " (builtins.map (s: "--with-${s}") [
      "system" "filesystem" "thread" "chrono" "date_time" 
      "program_options" "regex" "iostreams" "context"
    ])}
'';

    installPhase = ''
      mkdir -p $out/lib $out/include
      ./b2 install \
        variant=release \
        link=shared,static \
        threading=multi \
        runtime-link=shared \
        --prefix=$out \
        --libdir=$out/lib \
        --includedir=$out/include
    '';
  });
in
pkgs.mkShell {
  buildInputs = [
    boostBoth
    pkgs.gcc
    pkgs.cmake
    pkgs.libaio
    pkgs.pcre2
    pkgs.liburing
    pkgs.libnvme
    pkgs.tbb
    pkgs.ncurses
    pkgs.gflags
  ];
  
  shellHook = ''
    export BOOST_ROOT="${boostBoth}"
    export BOOST_LIBRARYDIR="${boostBoth}/lib"
    export BOOST_INCLUDEDIR="${boostBoth}/include"
    export LIBRARY_PATH="${boostBoth}/lib:$LIBRARY_PATH"
    export LD_LIBRARY_PATH="${boostBoth}/lib:$LD_LIBRARY_PATH"

    # Add libaio to library paths
    export LIBRARY_PATH="${pkgs.libaio}/lib:${pkgs.pcre2}/lib:${boostBoth}/lib:$LIBRARY_PATH"
    export LD_LIBRARY_PATH="${pkgs.libaio}/lib:${pkgs.pcre2}/lib:${boostBoth}/lib:$LD_LIBRARY_PATH"
    export CMAKE_PREFIX_PATH="${pkgs.libaio}:${pkgs.pcre2}:${boostBoth}:$CMAKE_PREFIX_PATH"

    echo "╔════════════════════════════════════════════════════════════════╗"
    echo "║                    LIBRARY PATHS                               ║"
    echo "╚════════════════════════════════════════════════════════════════╝"
    echo ""
    
    echo "=== BOOST ==="
    echo "Path: ${boostBoth}"
    echo "Lib dir: ${boostBoth}/lib"
    echo "Include dir: ${boostBoth}/include"
    echo ""
    
    echo "=== LIBAIO ==="
    echo "Path: ${pkgs.libaio}"
    echo "Lib dir: ${pkgs.libaio}/lib"
    echo ""
    
    echo "=== PCRE2 ==="
    echo "Path: ${pkgs.pcre2}"
    echo "Lib dir: ${pkgs.pcre2}/lib"
    echo ""
    
    echo "=== LIBURING ==="
    echo "Path: ${pkgs.liburing}"
    echo "Lib dir: ${pkgs.liburing}/lib"
    echo ""
    
    echo "=== LIBNVME ==="
    echo "Path: ${pkgs.libnvme}"
    echo "Lib dir: ${pkgs.libnvme}/lib"
    echo ""
    
    echo "=== TBB ==="
    echo "Path: ${pkgs.tbb}"
    echo "Lib dir: ${pkgs.tbb}/lib"
    echo ""
    
    echo "=== GFLAGS ==="
    echo "Path: ${pkgs.gflags}"
    echo "Lib dir: ${pkgs.gflags}/lib"
    echo ""
    
    echo "=== NCURSES ==="
    echo "Path: ${pkgs.ncurses}"
    echo "Lib dir: ${pkgs.ncurses}/lib"
    echo ""
    
    echo "=== GCC ==="
    echo "Path: ${pkgs.gcc}"
    echo "Lib dir: ${pkgs.gcc}/lib"
    echo ""
    
    echo "╔════════════════════════════════════════════════════════════════╗"
    echo "║                    BOOST LIBRARIES                             ║"
    echo "╚════════════════════════════════════════════════════════════════╝"
    echo ""
    
    if [ -d "${boostBoth}/lib" ]; then
      echo "Shared libraries (.so):"
      find ${boostBoth}/lib -name "*.so*" -type f | while read lib; do
        echo "  $(basename $lib) -> $lib"
      done
      echo ""
      
      echo "Static libraries (.a):"
      find ${boostBoth}/lib -name "*.a" -type f | while read lib; do
        echo "  $(basename $lib) -> $lib"
      done
    else
      echo "⚠ Boost lib directory not found!"
    fi
    
    echo ""
    echo "╔════════════════════════════════════════════════════════════════╗"
    echo "║                    ALL LIBRARY FILES                           ║"
    echo "╚════════════════════════════════════════════════════════════════╝"
    echo ""
    
    # Function to list libraries in a directory
    list_libs() {
      local name=$1
      local path=$2
      if [ -d "$path" ]; then
        echo "=== $name ==="
        ls -lh "$path"/*.{so*,a} 2>/dev/null | awk '{print "" $9 ": " $9}'
        echo ""
      fi
    }
    
    list_libs "Boost" "${boostBoth}/lib"
    list_libs "libaio" "${pkgs.libaio}/lib"
    list_libs "pcre2" "${pkgs.pcre2}/lib"
    list_libs "liburing" "${pkgs.liburing}/lib"
    list_libs "libnvme" "${pkgs.libnvme}/lib"
    list_libs "TBB" "${pkgs.tbb}/lib"
    list_libs "gflags" "${pkgs.gflags}/lib"
    list_libs "ncurses" "${pkgs.ncurses}/lib"
    
    echo "╔════════════════════════════════════════════════════════════════╗"
    echo "║                COPY COMMANDS (for migration)                   ║"
    echo "╚════════════════════════════════════════════════════════════════╝"
    echo ""
    echo "# Copy all Boost libraries:"
    echo "cp -r ${boostBoth}/lib/* /destination/path/"
    echo ""
    echo "# Copy specific library sets:"
    echo "cp ${pkgs.libaio}/lib/*.so* /destination/path/"
    echo "cp ${pkgs.pcre2}/lib/*.so* /destination/path/"
    echo "cp ${pkgs.liburing}/lib/*.so* /destination/path/"
    echo "cp ${pkgs.libnvme}/lib/*.so* /destination/path/"
    echo "cp ${pkgs.tbb}/lib/*.so* /destination/path/"
    echo "cp ${pkgs.gflags}/lib/*.so* /destination/path/"
    echo "cp ${boostBoth}/lib/*.so* /destination/path/"
    echo ""
    echo "# Copy all at once:"
    cat << 'EOF'
mkdir -p /destination/libs
for libpath in ${boostBoth}/lib ${pkgs.libaio}/lib ${pkgs.pcre2}/lib ${pkgs.liburing}/lib ${pkgs.libnvme}/lib ${pkgs.tbb}/lib ${pkgs.gflags}/lib; do
  cp -L $libpath/*.so* /destination/libs/ 2>/dev/null || true
done
EOF
    echo ""
  '';
}