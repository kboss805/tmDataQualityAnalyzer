# Security policy

## Reporting a vulnerability

Please report a security problem **privately**, by email, not in a public issue or pull request:

**kevin.bossoletti@gmail.com**

Put "tmDataQualityAnalyzer security" in the subject. Include what you found, the version
(**☰ menu > Help > About**, or the file name of the installer or ZIP), and the steps to reproduce it. If it involves a
file, a description of its shape is enough; please do not attach a recording that is sensitive or export-controlled.

This is a one-person project, so replies are best effort: expect an acknowledgement within about a week. A confirmed
problem is fixed in a new release, and the release notes say so, crediting you if you wish.

## What counts

The application reads files you open (IRIG 106 Chapter 10 recordings, exported CSV, TOML settings and templates) and writes
what you export. A flaw that lets a crafted file run code, crash the process in a way that can be exploited, read or write
outside the folders you chose, or tamper with the installer or the signed binaries is in scope. A problem in a bundled
library is in scope when this application is what exposes it.

The application makes no network connections of its own and runs without elevated rights; only the installer can ask for
them, when you choose to install for all users.

## Supported versions

Only the latest release, on the [releases page](https://github.com/kboss805/tmDataQualityAnalyzer/releases), is supported.
Fixes are not back-ported. The installer and the portable ZIP are code-signed: check the signature before you run one,
and take the files only from that page.
