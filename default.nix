{ }:


let
  rev = "9cc28832860772cd586d9f322d3882064dd74f5f";
  nixpkgs = builtins.fetchTarball {
    name = "nixpkgs-release-20.09";
    url = "https://github.com/nixos/nixpkgs/tarball/${rev}";
    # sha256 = "178dr6bz71lbv0ljynvvkrc2p0lwqmci482brkqdw9qfx3sc1a7f";
  };
  pkgs = import nixpkgs {};
in

pkgs.stdenv.mkDerivation {
  name = "jslinux";
  buildInputs = with pkgs; [
    ccls
    rustup
    rustc
    lld_11
    clang_11
    llvm_11
    emscripten
    wabt
    binaryen
    wasmer
  ];
  # shellHook = ''
  # '';
}
