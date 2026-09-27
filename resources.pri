# The Qt resources compiled into BOTH the app and the test binary, shared so the
# two cannot drift. See resources.qrc for why it is a .qrc and not a file list.
#
# Before this, the tests compiled in NO resources at all: every icon in every
# widget under test was a null QIcon, and a wall of qt.svg warnings scrolled past
# on each run. A widget whose icon is missing still constructs and still lays out,
# so nothing failed - it simply drew nothing.

RESOURCES += $$PWD/resources.qrc
