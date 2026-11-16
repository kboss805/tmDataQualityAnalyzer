tmDataQualityAnalyzer - Portable Edition
=======================================

Run tmDataQualityAnalyzer.exe to launch the application. Nothing needs to be
installed first: everything it needs is in this folder.

Settings files (frame sync patterns, receiver parameters and calibration step
files) are in the settings/ folder next to the executable. Preferences (theme,
recent files, the last folders used) are kept in a tmDataQualityAnalyzer/
folder beside it, because of the file named "portable" here - nothing is written
to the Windows registry or to %APPDATA%. Delete "portable" and the application
keeps its preferences in %APPDATA%\tmDataQualityAnalyzer, as an installed copy
does.

Documentation: UserManual.html is the full illustrated manual, with a
walkthrough of each job - open it in any browser, or from the application under
Help > User Manual.

Source code: https://github.com/kboss805/tmDataQualityAnalyzer
License:     GPLv3 (see LICENSE.txt); the libraries it uses are listed, with
             their licences, in THIRD-PARTY-NOTICES.txt.
