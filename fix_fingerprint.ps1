# Fix corrupted fingerprint_data_manager.cpp - simpler approach
$content = Get-Content 'src/fingerprint_data_manager.cpp' -Raw -Encoding UTF8

# Fix double quotes
$content = $content -replace '""/', '"/'
$content = $content -replace '/fingerprint_,', '/fingerprint_users.csv",'
$content = $content -replace 'FILE_,', 'FILE_'

[System.IO.File]::WriteAllText('src/fingerprint_data_manager.cpp', $content, [System.Text.Encoding]::UTF8)
Write-Host "Done!"