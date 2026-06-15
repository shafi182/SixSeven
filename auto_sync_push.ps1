Write-Host "--- Memulai Sinkronisasi Git Otomatis ---" -ForegroundColor Cyan

# 1. Batalkan merge yang nyangkut (mengabaikan error jika tidak ada)
Write-Host "`n[1/5] Membersihkan proses merge yang menggantung..." -ForegroundColor Yellow
git merge --abort 2>$null

# 2. Menambahkan perubahan lokal (berjaga-jaga jika ada file yang belum masuk)
Write-Host "`n[2/5] Menambahkan perubahan file lokal..." -ForegroundColor Yellow
git add .

# 3. Commit perubahan lokal sebelum pull
Write-Host "`n[3/5] Menyimpan perubahan lokal (commit)..." -ForegroundColor Yellow
git commit -m "Auto-commit sebelum sinkronisasi" 2>$null

# 4. Menarik data terbaru dari GitHub dengan rebase untuk menghindari merge commit yang berantakan
Write-Host "`n[4/5] Menarik (pull) data terbaru dari GitHub..." -ForegroundColor Yellow
git pull origin main --rebase

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[!] Peringatan: Gagal melakukan pull. Ada conflict yang harus diselesaikan manual di VS Code!" -ForegroundColor Red
    Write-Host "Setelah conflict diselesaikan, jalankan: git rebase --continue" -ForegroundColor Red
    exit
}

# 5. Mendorong ke GitHub
Write-Host "`n[5/5] Mendorong (push) kode ke GitHub..." -ForegroundColor Yellow
git push origin main

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n--- Berhasil Sinkronisasi dan Push ke GitHub! ---" -ForegroundColor Green
} else {
    Write-Host "`n--- Gagal Push ke GitHub. Silakan cek pesan error di atas. ---" -ForegroundColor Red
}
