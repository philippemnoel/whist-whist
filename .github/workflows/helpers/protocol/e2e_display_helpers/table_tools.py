#!/usr/bin/env python3

import os
import sys
from pytablewriter import MarkdownTableWriter
from contextlib import redirect_stdout
from datetime import datetime, timedelta

# add the current directory to the path no matter where this is called from
sys.path.append(os.path.join(os.getcwd(), os.path.dirname(__file__), "."))


def generate_no_comparison_table(
    results_file, experiment_metadata, most_interesting_metrics, client_metrics, server_metrics
):
    """
    Create a Markdown table to display the client and server metrics for the current run.
    Also include a table with the metadate of the run, and a table with the most interesting
    metrics. Do not include comparisons with other runs.
    Args:
        results_file (file): The open file where we want to save the markdown table
        experiment_metadata (dict): The metadata key-value pairs for the current run
        most_interesting_metrics (list): list of metrics that we want to display at the top (if found)
        client_metrics (dict): the client key-value pairs for the current run
        server_metrics (dict): the server key-value pairs for the current run
    Returns:
        None
    """
    experiment_metrics = [{"CLIENT": client_metrics}, {"SERVER": server_metrics}]
    with redirect_stdout(results_file):
        # Generate metadata table
        print("<details>")
        print("<summary>Experiment metadata - Expand here</summary>")
        print("\n")

        print("###### Experiment metadata: ######\n")
        writer = MarkdownTableWriter(
            # table_name="Interesting metrics",
            headers=["Key (this run)", "Value (this run)", "Value (compared run)"],
            value_matrix=[
                [
                    k,
                    experiment_metadata[k],
                    "N/A",
                ]
                for k in experiment_metadata
            ],
            margin=1,  # add a whitespace for both sides of each cell
        )
        writer.write_table()
        print("\n")
        print("</details>")
        print("\n")

        print("<details>")
        print("<summary>Experiment results - Expand here</summary>")
        print("\n")

        # Generate most interesting metric dictionary
        interesting_metrics = {}
        for _, metrics in experiment_metrics:
            for k in metrics:
                if k in most_interesting_metrics:
                    interesting_metrics[k] = metrics[k]
        experiment_metrics.insert(0, {"MOST INTERESTING": interesting_metrics})

        # Generate tables for most interesting metrics, client metrics, and server metrics
        for name, dictionary in experiment_metrics:
            if len(dictionary) == 0:
                print(f"NO {name} METRICS\n")
            else:
                print(f"###### {name} METRICS: ######\n")

                writer = MarkdownTableWriter(
                    # table_name="Interesting metrics",
                    headers=["Metric", "Entries", "Average", "Min", "Max"],
                    value_matrix=[
                        [
                            k,
                            dictionary[k]["entries"],
                            f"{dictionary[k]['avg']:.3f}",
                            dictionary[k]["min"],
                            dictionary[k]["max"],
                        ]
                        for k in dictionary
                    ],
                    margin=1,  # add a whitespace for both sides of each cell
                    max_precision=3,
                )
                writer.write_table()
                print("\n")

        print("\n")
        print("</details>")
        print("\n")


def generate_comparison_table(
    results_file,
    experiment_metadata,
    compared_experiment_metadata,
    compared_branch_name,
    most_interesting_metrics,
    client_metrics,
    server_metrics,
    compared_client_metrics,
    compared_server_metrics,
):
    """
    Create a Markdown table to display the client and server metrics for the current run,
    as well as those from a compared run. Also include a table with the metadata of the run,
    and a table with the most interesting metrics.
    Args:
        results_file (file): The open file where we want to save the markdown table
        most_interesting_metrics (list): list of metrics that we want to display at the top (if found)
        experiment_metadata (dict): The metadata key-value pairs for the current run
        compared_experiment_metadata (dict): The metadata key-value pairs for the compared run
        client_table_entries (list): the table entries for the client table
        server_table_entries (list): the table entries for the server table
        branch_name (str): the name of the branch for the compared run
    Returns:
        None
    """

    client_table_entries, server_table_entries = compute_deltas(
        client_metrics, server_metrics, compared_client_metrics, compared_server_metrics
    )

    table_entries = [{"CLIENT": client_table_entries}, {"SERVER": server_table_entries}]

    with redirect_stdout(results_file):
        # Generate metadata table
        print("<details>")
        print("<summary>Experiment metadata - Expand here</summary>")
        print("\n")

        print("###### Experiment metadata: ######\n")
        writer = MarkdownTableWriter(
            # table_name="Interesting metrics",
            headers=["Key (this run)", "Value (this run)", "Value (compared run)"],
            value_matrix=[
                [
                    k,
                    experiment_metadata[k],
                    "not found"
                    if not compared_experiment_metadata or k not in compared_experiment_metadata
                    else compared_experiment_metadata[k],
                ]
                for k in experiment_metadata
            ],
            margin=1,  # add a whitespace for both sides of each cell
        )
        writer.write_table()
        print("\n")
        print("</details>")
        print("\n")

        print("<details>")
        print("<summary>Experiment results - Expand here</summary>")
        print("\n")

        # Generate most interesting metric dictionary
        interesting_metrics = [
            row
            for row in entries
            for _, entries in table_entries
            if row[0] in most_interesting_metrics
        ]
        table_entries.insert(0, {"MOST INTERESTING": interesting_metrics})

        # Generate tables for most interesting metrics, client metrics, and server metrics
        for name, entries in table_entries:
            if len(entries) == 0:
                print(f"NO {name} METRICS\n")
            else:
                print(f"###### SUMMARY OF {name} METRICS: ######\n")

                writer = MarkdownTableWriter(
                    # table_name="Interesting metrics",
                    headers=[
                        "Metric",
                        "Entries (this branch)",
                        "Average (this branch)",
                        f"Average ({branch_name})",
                        "Delta",
                        "",
                    ],
                    value_matrix=[i for i in entries],
                    margin=1,  # add a whitespace for both sides of each cell
                    max_precision=3,
                )
                writer.write_table()
                print("\n")

        print("\n")
        print("</details>")
        print("\n")
