// 服务器状态更新
document.addEventListener('DOMContentLoaded', function() {
    // 格式化日期时间
    function formatDateTime(timestamp) {
        const date = new Date(timestamp);
        return date.toLocaleString();
    }

    // 格式化文件大小
    function formatFileSize(bytes) {
        if (bytes === 0) return '0 Bytes';
        const k = 1024;
        const sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
    }

    // 更新服务器状态
    function updateServerStatus() {
        fetch('/api/server/status')
            .then(response => {
                if (!response.ok) {
                    throw new Error('服务器状态获取失败');
                }
                return response.json();
            })
            .then(data => {
                // 更新状态数据
                document.getElementById('active-users').textContent = data.clientCount || 0;
                
                // 计算CPU使用率
                const cpuUsage = Math.round((data.cpuUsage.user + data.cpuUsage.system) / 1000000); // 转换为毫秒
                document.getElementById('cpu-usage').textContent = cpuUsage + '%';
                
                // 计算内存使用率
                const memoryUsage = Math.round((data.memoryUsage.rss / data.totalMemory) * 100);
                document.getElementById('memory-usage').textContent = memoryUsage + '%';
                
                // 更新上次更新时间
                document.getElementById('last-update').textContent = formatDateTime(data.timestamp);
                
                // 更新服务器状态指示器
                const statusValue = document.querySelector('.status-value.online');
                statusValue.textContent = '在线';
                statusValue.className = 'status-value online';
            })
            .catch(error => {
                console.error('更新状态失败:', error);
                
                // 更新服务器状态为离线
                const statusValue = document.querySelector('.status-value.online');
                statusValue.textContent = '离线';
                statusValue.className = 'status-value offline';
            });
    }

    // 获取客户端版本信息
    function getClientVersion() {
        fetch('/api/client/version')
            .then(response => {
                if (!response.ok) {
                    throw new Error('客户端版本信息获取失败');
                }
                return response.json();
            })
            .then(data => {
                // 更新版本信息
                document.getElementById('server-version').textContent = data.version;
                document.getElementById('build-date').textContent = formatDateTime(new Date(data.buildDate));
            })
            .catch(error => {
                console.error('获取版本信息失败:', error);
            });
    }

    // 获取下载文件大小
    function getDownloadSize() {
        fetch('/download/client', { method: 'HEAD' })
            .then(response => {
                if (!response.ok) {
                    return;
                }
                const contentLength = response.headers.get('content-length');
                if (contentLength) {
                    document.getElementById('download-size').textContent = formatFileSize(parseInt(contentLength));
                }
            })
            .catch(error => {
                console.error('获取文件大小失败:', error);
            });
    }

    // 初始化更新
    updateServerStatus();
    getClientVersion();
    getDownloadSize();

    // 定期更新状态
    setInterval(updateServerStatus, 30000); // 每30秒更新一次
});