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
    modelsPath: path.join(__dirname, '../../models'),
    configsPath: path.join(__dirname, '../../configs'),
    maxLogSize: 10 * 1024 * 1024 // 10MB
};

// 创建Express应用
const app = express();

// 检查主服务器可执行文件
function checkMainServer() {
    const binPath = path.join(__dirname, '../../bin');
    let serverExecutable = null;
    
    // 查找可能的服务器可执行文件
    const possibleNames = ['yolo_server', 'server', 'yolo_fps_assist'];
    for (const name of possibleNames) {
        const exePath = path.join(binPath, name);
        if (fs.existsSync(exePath)) {
            serverExecutable = exePath;
            break;
        }
    }
    
    return serverExecutable;
}

// 创建必要的目录
for (const dir of [
    config.downloadsPath, 
    config.uploadsPath, 
    config.logsPath, 
    config.modelsPath,
    config.configsPath,
    path.join(__dirname, '../../bin')
]) {
    if (!fs.existsSync(dir)) {
        console.log(`创建目录: ${dir}`);
        fs.mkdirSync(dir, { recursive: true });
    }
}

// 检查客户端目录
if (!fs.existsSync(config.clientPath)) {
    // 如果客户端目录不存在，创建一个
    fs.mkdirSync(config.clientPath, { recursive: true });
    console.log(`警告: 客户端目录 ${config.clientPath} 不存在, 已创建空目录`);
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

// 跟踪最近的ZIP创建时间
let lastZipCreationTime = 0;
const ZIP_REFRESH_INTERVAL = 3600000; // 一小时，单位: 毫秒

// 下载客户端
app.get('/download/client', (req, res) => {
    const zipFilePath = path.join(config.downloadsPath, 'zero-latency-client.zip');
    const currentTime = Date.now();
    
    // 检查ZIP文件是否存在且未过期
    if (fs.existsSync(zipFilePath) && 
        (currentTime - lastZipCreationTime) < ZIP_REFRESH_INTERVAL) {
        // 直接提供现有ZIP文件
        return res.download(zipFilePath, 'zero-latency-client.zip');
    }
    
    // 创建客户端目录 (如果不存在)
    const clientDir = path.join(config.downloadsPath, 'zero-latency-client');
    if (!fs.existsSync(clientDir)) {
        fs.mkdirSync(clientDir, { recursive: true });
        fs.mkdirSync(path.join(clientDir, 'configs'), { recursive: true });
    }
    
    // 创建示例/默认配置文件
    const clientConfigPath = path.join(clientDir, 'configs', 'client.json');
    if (!fs.existsSync(clientConfigPath)) {
        // 查找源配置
        const sourceConfigPath = path.join(config.configsPath, 'client.json');
        if (fs.existsSync(sourceConfigPath)) {
            fs.copyFileSync(sourceConfigPath, clientConfigPath);
        } else {
            // 创建默认配置
            const defaultConfig = {
                "server_ip": "127.0.0.1",
                "server_port": 7788,
                "game_id": 1,
                "target_fps": 60,
                "screen_width": 800,
                "screen_height": 600,
                "auto_connect": true,
                "auto_start": false,
                "enable_aim_assist": true,
                "enable_esp": true,
                "enable_recoil_control": true
            };
            fs.writeFileSync(clientConfigPath, JSON.stringify(defaultConfig, null, 2));
        }
    }
    
    // 创建示例批处理文件
    const batchFilePath = path.join(clientDir, 'start.bat');
    fs.writeFileSync(batchFilePath, '@echo off\necho 正在启动零延迟YOLO FPS云辅助系统客户端...\nstart "" "yolo_client.exe"\n');
    
    // 创建README文件
    const readmePath = path.join(clientDir, 'README.txt');
    fs.writeFileSync(readmePath, '零延迟YOLO FPS云辅助系统 - Windows客户端\n\n使用方法:\n1. 双击start.bat启动程序\n2. 配置文件位于configs目录\n');
    
    // 创建输出ZIP流
    const output = fs.createWriteStream(zipFilePath);
    const archive = archiver('zip', {
        zlib: { level: 9 } // 最大压缩级别
    });

    output.on('close', () => {
        console.log(`客户端已打包: ${archive.pointer()} 字节`);
        lastZipCreationTime = currentTime;
        res.download(zipFilePath, 'zero-latency-client.zip');
    });

    archive.on('error', (err) => {
        console.error('打包客户端失败:', err);
        res.status(500).send('打包客户端失败');
    });

    archive.pipe(output);

    // 添加客户端文件
    if (fs.existsSync(config.clientPath) && fs.readdirSync(config.clientPath).length > 0) {
        archive.directory(config.clientPath, 'zero-latency-client');
    } else {
        // 如果客户端目录为空，使用之前创建的临时目录
        archive.directory(clientDir, false);
    }
    
    // 添加配置文件
    const configPath = path.join(config.configsPath, 'client.json');
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
    // 检查服务器可执行文件
    const serverExecutable = checkMainServer();
    const serverRunning = serverExecutable !== null;
    
    // 获取CPU使用率
    const cpuUsage = process.cpuUsage();
    
    // 构建响应
    const status = {
        timestamp: Date.now(),
        hostname: os.hostname(),
        platform: os.platform(),
        uptime: os.uptime(),
        cpuUsage: process.cpuUsage(),
        memoryUsage: process.memoryUsage(),
        totalMemory: os.totalmem(),
        freeMemory: os.freemem(),
        clientCount: 0, // 需要从主服务获取
        serverExecutable: serverExecutable,
        serverRunning: serverRunning
    };
    
    // 如果服务器可执行文件存在，尝试读取日志获取更多信息
    if (serverRunning) {
        const logsDir = path.join(__dirname, '../../logs');
        if (fs.existsSync(logsDir)) {
            const logFiles = fs.readdirSync(logsDir)
                .filter(file => file.endsWith('.log'))
                .map(file => path.join(logsDir, file))
                .sort((a, b) => {
                    return fs.statSync(b).mtime.getTime() - fs.statSync(a).mtime.getTime();
                });
            
            if (logFiles.length > 0) {
                try {
                    const latestLog = fs.readFileSync(logFiles[0], 'utf8');
                    // 解析日志中的客户端连接信息
                    const clientMatch = latestLog.match(/客户端 #(\d+) 连接/g);
                    if (clientMatch) {
                        status.clientCount = clientMatch.length;
                    }
                } catch (err) {
                    console.error('读取日志文件失败:', err);
                }
            }
        }
    }

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
            const filePath = path.join(config.logsPath, file);
            const stats = fs.statSync(filePath);
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
    const logName = req.params.name;
    
    // 安全检查，防止路径遍历
    if (logName.includes('..') || logName.includes('/') || logName.includes('\\')) {
        return res.status(400).send('无效的日志文件名');
    }
    
    const logPath = path.join(config.logsPath, logName);
    
    if (!fs.existsSync(logPath)) {
        return res.status(404).send('日志文件不存在');
    }

    // 检查文件大小
    const stats = fs.statSync(logPath);
    if (stats.size > config.maxLogSize) {
        return res.status(413).send('日志文件过大，无法查看');
    }

    try {
        const content = fs.readFileSync(logPath, 'utf8');
        res.send(content);
    } catch (err) {
        console.error('读取日志文件失败:', err);
        res.status(500).send('服务器内部错误');
    }
});

// 获取模型列表
app.get('/api/models', (req, res) => {
    if (!fs.existsSync(config.modelsPath)) {
        return res.json([]);
    }

    const models = fs.readdirSync(config.modelsPath)
        .filter(file => file.endsWith('.onnx'))
        .map(file => {
            const filePath = path.join(config.modelsPath, file);
            const stats = fs.statSync(filePath);
            return {
                name: file,
                size: stats.size,
                date: stats.mtime
            };
        })
        .sort((a, b) => b.date - a.date);

    res.json(models);
});

// 模型上传
app.post('/api/models/upload', upload.single('model'), (req, res) => {
    if (!req.file) {
        return res.status(400).send('未提供模型文件');
    }

    if (!req.file.originalname.endsWith('.onnx')) {
        fs.unlinkSync(req.file.path); // 删除临时文件
        return res.status(400).send('模型文件必须是.onnx格式');
    }

    const modelPath = path.join(config.modelsPath, req.file.originalname);
    
    try {
        fs.copyFileSync(req.file.path, modelPath);
        fs.unlinkSync(req.file.path); // 删除临时文件
        
        console.log(`上传了新模型: ${req.file.originalname}`);
        res.status(200).send('模型上传成功');
    } catch (err) {
        console.error('保存模型文件失败:', err);
        res.status(500).send('服务器内部错误');
    }
});

// 错误处理中间件
app.use((err, req, res, next) => {
    console.error('服务器错误:', err);
    res.status(500).send('服务器内部错误');
});

// 启动服务器
app.listen(config.port, () => {
    console.log(`Web服务器运行在端口 ${config.port}`);
    console.log(`访问地址: http://localhost:${config.port}`);
    
    // 检查服务器可执行文件
    const serverExecutable = checkMainServer();
    if (serverExecutable) {
        console.log(`检测到服务器可执行文件: ${serverExecutable}`);
    } else {
        console.log('警告: 未找到服务器可执行文件，请先编译主服务器');
    }
});