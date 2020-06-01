from app.logger import *
from app import *


def setBranchVersion(branch, version):
    """Maps disk branch to client version. Master is latest stable version, staging is second latest version

    Args:
        branch (str): The name of the branch
        version (str): The version number of the client application
    """
    command = text(
        """
        UPDATE versions
        SET version = :version
        WHERE
        "branch" = :branch
        """
    )
    params = {"branch": branch, "version": version}
    with engine.connect() as conn:
        conn.execute(command, **params)
        conn.close()
