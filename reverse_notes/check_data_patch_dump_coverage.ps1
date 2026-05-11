param(
    [string]$InventoryPath = "F:\VSProjects\Trials-Fusion-Mod\reverse_notes\data_patch_inventory.csv",
    [string]$DumpDir = "F:\Trials Fusion\datapack\pak_runtime\data_patch_named",
    [string]$OutputPath = "F:\VSProjects\Trials-Fusion-Mod\reverse_notes\data_patch_dump_coverage.csv"
)

if (-not (Test-Path -LiteralPath $InventoryPath)) {
    throw "Inventory not found: $InventoryPath"
}

if (-not (Test-Path -LiteralPath $DumpDir)) {
    throw "Dump directory not found: $DumpDir"
}

function Get-SafeDumpName {
    param(
        [int]$Index,
        [string]$Name
    )

    $safeName = $Name -replace '[^A-Za-z0-9._-]', '_'
    return "{0:D3}_{1}" -f $Index, $safeName
}

$inventory = Import-Csv -LiteralPath $InventoryPath
$coverage = foreach ($entry in $inventory) {
    $index = [int]$entry.Index

    if ($index -eq 543) {
        continue
    }

    $expectedName = Get-SafeDumpName -Index $index -Name $entry.Name
    $dumpPath = Join-Path $DumpDir $expectedName
    $dumpFile = Get-Item -LiteralPath $dumpPath -ErrorAction SilentlyContinue
    $expandedSize = [int64]$entry.ExpandedSize

    $status = "missing"
    $actualSize = 0L
    if ($dumpFile) {
        $actualSize = [int64]$dumpFile.Length
        if ($actualSize -eq $expandedSize) {
            $status = "ok"
        }
        elseif ($actualSize -lt $expandedSize) {
            $status = "partial"
        }
        else {
            $status = "oversized"
        }
    }

    [PSCustomObject]@{
        Status = $status
        Index = $index
        Name = $entry.Name
        Extension = $entry.Extension
        Flag = $entry.Flag
        StoredSize = [int64]$entry.StoredSize
        ExpandedSize = $expandedSize
        ActualSize = $actualSize
        MissingBytes = [Math]::Max(0L, $expandedSize - $actualSize)
        DumpPath = $dumpPath
    }
}

$coverage | Export-Csv -LiteralPath $OutputPath -NoTypeInformation

"Coverage written to $OutputPath"
$coverage | Group-Object Status | Sort-Object Name | Select-Object Name, Count | Format-Table -AutoSize
""
"By extension:"
$coverage | Group-Object Extension, Status | Sort-Object Name | Select-Object Name, Count | Format-Table -AutoSize
""
"Largest missing/partial entries:"
$coverage |
    Where-Object { $_.Status -ne "ok" } |
    Sort-Object MissingBytes -Descending |
    Select-Object -First 30 Status, Index, Extension, ExpandedSize, ActualSize, MissingBytes, Name |
    Format-Table -AutoSize
