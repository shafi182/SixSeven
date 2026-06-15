Write-Host "--- Memulai Proses Sinkronisasi dan Push ---" -ForegroundColor Cyan

# 1. Batalkan merge yang menggantung (jika ada)
git merge --abort 2>$null

# 2. Tambahkan semua perubahan file
Write-Host "`n[1/4] Menambahkan file yang diubah..." -ForegroundColor Yellow
git add .

# 3. Lakukan commit
Write-Host "[2/4] Melakukan commit lokal..." -ForegroundColor Yellow
git commit -m "Auto-update & sync" 2>$null

# 4. Tarik data terbaru dari GitHub (untuk mencegah rejected non-fast-forward)
Write-Host "[3/4] Menarik data dari GitHub (Pull dengan Rebase)..." -ForegroundColor Yellow
git pull origin main --rebase

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[!] ERROR: Ada conflict saat pull." -ForegroundColor Red
    Write-Host "Buka file yang merah di VS Code, selesaikan conflict-nya, lalu jalankan: git rebase --continue" -ForegroundColor Red
    exit
}

# 5. Push ke GitHub
Write-Host "`n[4/4] Mendorong kode ke GitHub (Push)..." -ForegroundColor Yellow
git push origin main

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n--- SUCCESS: Semua kode berhasil di-push ke GitHub! ---" -ForegroundColor Green
} else {
    Write-Host "`n--- FAILED: Gagal melakukan push. Periksa error di atas. ---" -ForegroundColor Red
}
