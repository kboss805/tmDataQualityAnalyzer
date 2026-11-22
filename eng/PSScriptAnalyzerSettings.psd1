@{
    # Errors and warnings fail eng\check_scripts.ps1; information findings (positional parameters on a cmdlet, say) do not.
    Severity     = @('Error', 'Warning')

    ExcludeRules = @(
        # These are console scripts whose job is to tell the person running them what happened, in colour: Write-Host is the
        # right call, not an output that should be capturable. (The scripts that return values do so with return.)
        'PSAvoidUsingWriteHost'
    )
}
