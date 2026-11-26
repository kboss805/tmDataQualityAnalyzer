@{
    # Errors and warnings fail eng\check_scripts.ps1; information findings (positional parameters on a cmdlet, say) do not.
    # ParseError is its own severity, and without it here a script that does not parse (a missing closing brace) is NOT a
    # finding: a test script with one passed this check and then failed to start in a clean Sandbox, twice.
    Severity     = @('ParseError', 'Error', 'Warning')

    ExcludeRules = @(
        # These are console scripts whose job is to tell the person running them what happened, in colour: Write-Host is the
        # right call, not an output that should be capturable. (The scripts that return values do so with return.)
        'PSAvoidUsingWriteHost'
    )
}
