#!/usr/bin/env python
import os
from functools import wraps, partial
import sys

# add the current directory to the path no matter where this is called from
sys.path.append(os.path.join(os.getcwd(), os.path.dirname(__file__), "."))

from github import Github

import formatters as fmt


def github_comment_update(
    github_token: str,
    github_repo: str,
    issue_number: int,
    identifier: str,
    body: str,
    title=None,
    code=None,
    lang=None,
    create=True,
):
    """Updates and creates previous comments on a GitHub issue or PR

    A IDENTIFIER must be passed as the second argument, which will be hidden
    in the markup of the comment to identify it to later runs of your script.

    If this function finds one or more comments in the thread containing
    IDENTIFIER, it will replace the contents of that comment with BODY.

    Args:
        github_token: to authenticate with Github
        github_repo: repo to comment on
        issue: a number that represents the issue or pull request
        identifier: a string that represents 'instances' of this comment
        body: a string, the main content of the comment
        create: True if you want to create a comment IF none exist currently
        title: a optional string, formatted at the top of the comment
        code: a optional string, placed in a block at the bottom of the comment
        lang: a optional string, used to format the comment's code block
    Returns:
        None
    """

    client = Github(github_token)
    repo = client.get_repo(github_repo)

    content = fmt.default_message_github(body, title, code, lang)
    body = fmt.concat_id(identifier, content)

    issue = repo.get_issue(issue_number)
    matching_comments = []
    for comment in issue.get_comments():
        if fmt.startswith_id(identifier, comment.body):
            matching_comments.append(comment)

    if not matching_comments and create:
        # create a comment if none exist
        issue.create_comment(body)
    else:
        for comment in matching_comments:
            comment.edit(body)
