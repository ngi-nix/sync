{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = {nixpkgs, ...}: let
    system = "x86_64-linux";
    pkgs = import nixpkgs {inherit system;};
  in {
    packages.x86_64-linux.default = pkgs.callPackage ({
      stdenv,
      autoreconfHook,
      curl,
      gnunet,
      jansson,
      libgcrypt,
      libmicrohttpd,
      libsodium,
      pkg-config,
      postgresql,
      taler-exchange,
    }:
      stdenv.mkDerivation {
        name = "sync";
        version = "hello";
        src = ./.;

        nativeBuildInputs = [
          autoreconfHook
          curl
          taler-exchange
          gnunet
          jansson
          libgcrypt
          libmicrohttpd
          libsodium
          pkg-config
          postgresql
        ];

      }) {};
  };
}
