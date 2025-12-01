{ pkgs ? import <nixpkgs> {} }:
let
  # Use an older nixpkgs version with glibc < 2.36
  oldPkgs = import (builtins.fetchTarball {
    url = "https://github.com/NixOS/nixpkgs/archive/nixos-22.05.tar.gz";
    sha256 = "154x9swf494mqwi4z8nbq2f0sp8pwp4fvx51lqzindjfbb9yxxv5";
  }) {};

  boostBoth = oldPkgs.boost178.overrideAttrs (oldAttrs: {
    configurePhase = ''
      ./bootstrap.sh --prefix=$out
    '';
    
    buildPhase = ''
      # Build both shared and static libraries
      ./b2 -j$NIX_BUILD_CORES \
        variant=release \
        link=shared,static \
        threading=multi \
        runtime-link=shared \
        ${oldPkgs.lib.concatStringsSep " " (builtins.map (s: "--with-${s}") [
          "system" "filesystem" "thread" "chrono" "date_time" 
          "program_options" "regex" "iostreams"
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
oldPkgs.mkShell {
  buildInputs = [
    boostBoth
    oldPkgs.gcc
    oldPkgs.cmake
    oldPkgs.libaio
    oldPkgs.pcre2
    oldPkgs.liburing
    oldPkgs.ncurses
  ];
  
  shellHook = ''
    export BOOST_ROOT="${boostBoth}"
    export BOOST_LIBRARYDIR="${boostBoth}/lib"
    export BOOST_INCLUDEDIR="${boostBoth}/include"
    export LIBRARY_PATH="${boostBoth}/lib:$LIBRARY_PATH"
    export LD_LIBRARY_PATH="${boostBoth}/lib:$LD_LIBRARY_PATH"

    # Add libaio to library paths
    export LIBRARY_PATH="${oldPkgs.libaio}/lib:${oldPkgs.pcre2}/lib:${boostBoth}/lib:$LIBRARY_PATH"
    export LD_LIBRARY_PATH="${oldPkgs.libaio}/lib:${oldPkgs.pcre2}/lib:${boostBoth}/lib:$LD_LIBRARY_PATH"
    export CMAKE_PREFIX_PATH="${oldPkgs.libaio}:${oldPkgs.pcre2}:${boostBoth}:$CMAKE_PREFIX_PATH"

    echo "Using glibc version: ${oldPkgs.glibc.version}"
    echo "Boost libraries loaded from: ${boostBoth}/lib"
    echo ""
    echo "Shared libraries:"
    ls ${boostBoth}/lib/libboost_system.so* 2>/dev/null && echo "  ✓ libboost_system.so found" || echo "  ✗ libboost_system.so not found"
    echo ""
    echo "Static libraries:"
    ls ${boostBoth}/lib/libboost_system.a 2>/dev/null && echo "  ✓ libboost_system.a found" || echo "  ✗ libboost_system.a not found"
    echo ""
    echo "All Boost libraries:"
    ls -lh ${boostBoth}/lib/ | grep libboost
  '';
}