# Fuzz Corpus Seeds

This directory contains tiny project-owned smoke seeds for the standalone fuzz
harnesses. They are not conformance vectors and do not describe expected FFV1
behavior. Their purpose is only to keep the file-input path exercised by CTest.

Future retained regression corpus entries should be added deliberately, with a
short note describing the bug or boundary condition they preserve.
