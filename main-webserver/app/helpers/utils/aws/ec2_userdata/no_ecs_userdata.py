"""
This file/layer of indirection over the .sh file
exists so the userdata can be brought in via an import
(similar to the rest of our constants)
"""

import os

with open(os.path.join(os.path.dirname(__file__), "no_ecs_userdata.sh"), "r") as f:
    userdata_template = f.read()
