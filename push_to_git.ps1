# 1. Hitung jumlah commit saat ini dan tambah 1 untuk nomor urut baru
$commitCount = (git rev-list --count HEAD 2>$null)
if (-not $commitCount) { $commitCount = 0 }
$nextNumber = [int]$commitCount + 1

# 2. Ambil semua perubahan file
git add .

# 3. Lakukan commit dengan pesan berangka otomatis
git commit -m "Update #${nextNumber}: Push from Local"

# 4. Kirim ke GitHub
git push
