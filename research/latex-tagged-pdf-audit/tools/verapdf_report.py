#!/usr/bin/env python3
"""Run veraPDF and print a compact list of failed rules.

Usage:
    python3 -I tools/verapdf_report.py PATH/TO/verapdf PROFILE FILE.pdf

PROFILE is a veraPDF flavour id, e.g. ua2, ua1, 4f, wt1a, wt1r.
Prints `compliant: true|false` and one line per failed rule with the
specification, clause, test number and failed-check count.
"""
import re
import subprocess
import sys


def main():
    verapdf, profile, pdf = sys.argv[1:4]
    xml = subprocess.run([verapdf, "-f", profile, "--format", "xml", pdf],
                         capture_output=True, text=True).stdout
    status = re.search(r'isCompliant="(\w+)"', xml)
    profile_name = re.search(r'profileName="([^"]*)"', xml)
    print(f"profile: {profile_name.group(1) if profile_name else profile}")
    print(f"compliant: {status.group(1) if status else 'unknown'}")
    pattern = (r'<rule specification="([^"]*)" clause="([^"]*)" testNumber="([^"]*)" '
               r'status="failed" failedChecks="(\d+)"[^>]*>\s*<description>(.*?)</description>')
    for m in re.finditer(pattern, xml, re.S):
        desc = " ".join(m.group(5).split()).replace("&lt;", "<").replace("&gt;", ">")
        print(f"  FAIL {m.group(1)} clause {m.group(2)} test {m.group(3)} (x{m.group(4)}): {desc[:170]}")


if __name__ == "__main__":
    main()
