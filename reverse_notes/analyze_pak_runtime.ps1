param(
    [string]$RuntimeDir = "F:\Trials Fusion\datapack\pak_runtime",
    [string]$InventoryPath = "F:\VSProjects\Trials-Fusion-Mod\reverse_notes\data_patch_inventory.csv",
    [string]$OutputPath = "F:\VSProjects\Trials-Fusion-Mod\reverse_notes\data_patch_stream_matches.csv",
    [switch]$IncludeReadSummary
)

$streamsPath = Join-Path $RuntimeDir "pak_streams.csv"
$readsPath = Join-Path $RuntimeDir "pak_runtime_log.csv"

if (-not (Test-Path -LiteralPath $InventoryPath)) {
    throw "Inventory not found: $InventoryPath"
}

if (-not (Test-Path -LiteralPath $streamsPath)) {
    throw "Stream summary not found: $streamsPath"
}

$inventory = Import-Csv -LiteralPath $InventoryPath
$streams = Import-Csv -LiteralPath $streamsPath

function Convert-HexOrDecimalToInt64 {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return 0
    }

    if ($Value.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
        return [Convert]::ToInt64($Value.Substring(2), 16)
    }

    return [Convert]::ToInt64($Value)
}

$matches = foreach ($stream in $streams) {
    $streamBase = Convert-HexOrDecimalToInt64 $stream.stream_base
    $streamSize = Convert-HexOrDecimalToInt64 $stream.stream_size

    foreach ($entry in $inventory) {
        $entryOffset = Convert-HexOrDecimalToInt64 $entry.Offset
        $entryEnd = Convert-HexOrDecimalToInt64 $entry.End
        $storedSize = Convert-HexOrDecimalToInt64 $entry.StoredSize
        $expandedSize = Convert-HexOrDecimalToInt64 $entry.ExpandedSize

        $sizeMatch =
            $streamSize -eq $storedSize -or
            $streamSize -eq $expandedSize -or
            $streamSize -eq ($storedSize - 128)

        $offsetMatch =
            $streamBase -eq $entryOffset -or
            $streamBase -eq ($entryOffset + 128) -or
            ($streamBase -ge $entryOffset -and $streamBase -lt $entryEnd)

        if ($sizeMatch -or $offsetMatch) {
            $score = 0
            $reasons = @()
            if ($sizeMatch) {
                $score += 1
                $reasons += "size"
            }
            if ($offsetMatch) {
                $score += 2
                $reasons += "offset"
            }

            [PSCustomObject]@{
                Score = $score
                FirstReadIndex = $stream.first_read_index
                StreamInfoPtr = $stream.stream_info_ptr
                ModeFlag = $stream.mode_flag_70
                StreamBase = $stream.stream_base
                StreamSize = $streamSize
                MatchReason = ($reasons -join "+")
                EntryIndex = $entry.Index
                Name = $entry.Name
                Extension = $entry.Extension
                Hash = $entry.Hash
                StoredSize = $storedSize
                ExpandedSize = $expandedSize
                Flag = $entry.Flag
                EntryOffset = $entryOffset
                EntryEnd = $entryEnd
                FirstAsciiSample = $stream.ascii_sample
            }
        }
    }
}

$sortedMatches = $matches |
    Sort-Object -Property @{ Expression = "Score"; Descending = $true }, FirstReadIndex, EntryIndex

$sortedMatches | Export-Csv -LiteralPath $OutputPath -NoTypeInformation
$sortedMatches | Format-Table -AutoSize

if ($IncludeReadSummary -and (Test-Path -LiteralPath $readsPath)) {
    $reads = Import-Csv -LiteralPath $readsPath
    "`nRead summary:"
    $reads | Measure-Object | Select-Object Count
    $reads | Group-Object mode_flag_70 | Sort-Object Name | Select-Object Name, Count
}
