let deflate = nix build nixpkgs#libdeflate --no-link --print-out-paths
let magick = nix build nixpkgs#imagemagick --no-link --print-out-paths

gcc *.c -O3 -o map2mc -lm -msse2 -L$"($deflate)/lib/" -ldeflate
