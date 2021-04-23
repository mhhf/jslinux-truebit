{ }:


let
  rev = "f41a3e7d7d327ea66459d17bfbe4a751b2496cb1";
  nixpkgs = builtins.fetchTarball {
    name = "nixpkgs-release-20.09";
    url = "https://github.com/nixos/nixpkgs/tarball/${rev}";
    # sha256 = "178dr6bz71lbv0ljynvvkrc2p0lwqmci482brkqdw9qfx3sc1a7f";
  };
  pkgs = import nixpkgs {};
  real_pkgs = import <nixpkgs> {};
in

pkgs.stdenv.mkDerivation {
  name = "jslinux-master";
  buildInputs = with pkgs; [
    # ccls
    # rustup
    # rustc
    # lld_11
    clang
    # llvm_11
    emscripten
    real_pkgs.wabt
    # real_pkgs.binaryen
    # wasmer
  ];
  shellHook = ''
    export TRUEBIT_PATH=$HOME/src/truebit-eth
  '';
}
