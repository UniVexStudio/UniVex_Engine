# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Retarget: conforms characters and their animations to one humanoid skeleton. Runtime/ is the
# engine side (reference skeleton, bone matching, conforming) and never depends on the editor;
# Editor/ (the retarget window) lands with the editor phase and links the other way round.
add_subdirectory(Runtime)
