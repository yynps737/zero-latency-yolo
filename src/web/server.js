const express = require('express');
const path = require('path');
const fs = require('fs');
const os = require('os');
const archiver = require('archiver');
const multer = require('multer');
const bodyParser = require('body-parser');

// 配置
const config = {
    port: process.env.PORT || 3000,
    clientPath: path.join(__dirname, '../../build/client'),
    downloadsPath: path.join(__dirname, '../../downloads'),
    uploadsPath: path.join(__dirname, '../../uploads'),
    logsPath: path.join(__dirname, '../../logs'),
    maxLogSize: 10 * 1024 * 1024 // 10MB
};

// 创建Express应用
const app = express();

// 确保必要的目录存在
for (const dir of [config.downloadsPath, config.uploadsPath, config.logsPath]) {
    if (!fs.existsSync(dir)) {
        fs.mkdirSync(dir, { recursive: true });
    }
}

// 静态文件服务
app.use(express.static(path.join(__dirname, 'public')));
app.use('/downloads', express.static(config.downloadsPath));

// 中间件
app.use(bodyParser.json());
app.use(bodyParser.urlencoded({ extended: true }));

// 配置文件上传
const storage = multer.diskStorage({
    destination: (req, file, cb) => {
        cb(null, config.uploadsPath);
    },
    filename: (req, file, cb) => {
        cb(null, Date.now() + '-' + file.originalname);
    }
});
const upload = multer({ storage });

// 路由
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

// 下载客户端
app.get('/download/client', (req, res) => {
    const zipFilePath = path.join(config.downloadsPath, 'zero-latency-client.zip');
    const output = fs.createWriteStream(zipFilePath);
    const archive = archiver('zip', {
        zlib: { level: 9 } // 最大压缩级别
    });

    output.on('close', () => {
        console.log(`客户端已打包: ${archive.pointer()} 字节`);
        res.download(zipFilePath, 'zero-latency-client.zip');
    });

    archive.on('error', (err) => {
        console.error('打包客户端失败:', err);
        res.status(500).send('打包客户端失败');
    });

    archive.pipe(output);

    // 添加客户端文件
    archive.directory(config.clientPath, 'zero-latency-client');
    
    // 添加配置文件
    const configPath = path.join(__dirname, '../../configs/client.json');
    if (fs.existsSync(configPath)) {
        archive.file(configPath, { name: 'zero-latency-client/configs/client.json' });
    }

    archive.finalize();
});

// 上传日志
app.post('/upload/logs', upload.single('logfile'), (req, res) => {
    if (!req.file) {
        return res.status(400).send('未提供日志文件');
    }

    const logname = `client-${new Date().toISOString().replace(/[:.]/g, '-')}.log`;
    const logPath = path.join(config.logsPath, logname);

    fs.copyFileSync(req.file.path, logPath);
    fs.unlinkSync(req.file.path); // 删除临时文件

    console.log(`接收到客户端日志: ${logname}`);
    res.status(200).send('日志上传成功');
});

// 获取服务器信息
app.get('/api/server/status', (req, res) => {
    const status = {
        timestamp: Date.now(),
        hostname: os.hostname(),
        platform: os.platform(),
        uptime: os.uptime(),
        cpuUsage: process.cpuUsage(),
        memoryUsage: process.memoryUsage(),
        totalMemory: os.totalmem(),
        freeMemory: os.freemem(),
        clientCount: 0 // 需要从主服务获取
    };

    res.json(status);
});

// 获取最新客户端版本
app.get('/api/client/version', (req, res) => {
    const version = {
        version: '1.0.0',
        buildDate: new Date().toISOString(),
        minRequired: '1.0.0',
        changes: [
            '初始版本发布'
        ],
        downloadUrl: '/download/client'
    };

    res.json(version);
});

// 获取日志列表
app.get('/api/logs', (req, res) => {
    if (!fs.existsSync(config.logsPath)) {
        return res.json([]);
    }

    const logs = fs.readdirSync(config.logsPath)
        .filter(file => file.endsWith('.log'))
        .map(file => {
            const stats = fs.statSync(path.join(config.logsPath, file));
            return {
                name: file,
                size: stats.size,
                date: stats.mtime
            };
        })
        .sort((a, b) => b.date - a.date);

    res.json(logs);
});

// 查看日志内容
app.get('/api/logs/:name', (req, res) => {
    const logPath = path.join(config.logsPath, req.params.name);
    
    if (!fs.existsSync(logPath)) {
        return res.status(404).send('日志文件不存在');
    }

    // 检查文件大小
    const stats = fs.statSync(logPath);
    if (stats.size > config.maxLogSize) {
        return res.status(413).send('日志文件过大，无法查看');
    }

    const content = fs.readFileSync(logPath, 'utf8');
    res.send(content);
});

// 错误处理
app.use((err, req, res, next) => {
    console.error('服务器错误:', err);
    res.status(500).send('服务器内部错误');
});

// 启动服务器
app.listen(config.port, () => {
    console.log(`Web服务器运行在端口 ${config.port}`);
    console.log(`访问地址: http://localhost:${config.port}`);
});