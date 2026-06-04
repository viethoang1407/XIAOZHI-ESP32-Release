const WebSocket = require('ws');
const https = require('https');
const http = require('http');
const fs = require('fs');
const path = require('path');
const { exec, execSync } = require('child_process');

// Configuration
const CONFIG_FILE = path.join(__dirname, 'config.json');
let token = '';

// Load token from arguments or config.json
if (process.argv[2]) {
    token = process.argv[2];
    console.log(`Using token from command line argument.`);
    // Save to config.json for future runs
    fs.writeFileSync(CONFIG_FILE, JSON.stringify({ token }, null, 2));
} else if (fs.existsSync(CONFIG_FILE)) {
    try {
        const config = JSON.parse(fs.readFileSync(CONFIG_FILE, 'utf-8'));
        token = config.token || '';
        if (token) {
            console.log(`Loaded token from config.json`);
        }
    } catch (e) {
        console.error('Error reading config.json:', e);
    }
}

if (!token) {
    console.error('\n[ERROR] No Token provided!');
    console.error('Please run the script with your token:');
    console.error('  node mcp_bridge_host.js YOUR_TOKEN_HERE\n');
    process.exit(1);
}

// Strip protocol if user copied full URL
if (token.startsWith('wss://')) {
    const urlObj = new URL(token);
    token = urlObj.searchParams.get('token') || token;
}

const wssUrl = `wss://api.xiaozhi.me/mcp/?token=${token}`;
console.log(`Connecting to Xiaozhi MCP Endpoint...`);

let ws = null;
let reconnectTimer = null;

function connect() {
    if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
    }

    ws = new WebSocket(wssUrl);

    ws.on('open', () => {
        console.log('\n[SUCCESS] Connected to Xiaozhi MCP Tunnel!');
        console.log('Your PC Bridge is now online. Trợ lý ảo của bạn đã sẵn sàng tra cứu thông tin!\n');
    });

    ws.on('message', async (data) => {
        try {
            const message = JSON.parse(data.toString());
            console.log(`[INCOMING] Method: ${message.method}, Id: ${message.id}`);
            
            // Handle JSON-RPC messages
            if (message.jsonrpc === '2.0') {
                await handleRequest(message);
            }
        } catch (err) {
            console.error('Error handling message:', err);
        }
    });

    ws.on('close', (code, reason) => {
        console.log(`\n[WARNING] Connection closed (code: ${code}, reason: ${reason || 'none'}). Reconnecting in 5s...`);
        ws = null;
        reconnectTimer = setTimeout(connect, 5000);
    });

    ws.on('error', (err) => {
        console.error('[ERROR] WebSocket error:', err.message);
    });
}

// JSON-RPC Request Handler
async function handleRequest(request) {
    const { method, params, id } = request;

    if (method === 'initialize') {
        const response = {
            jsonrpc: '2.0',
            id: id,
            result: {
                protocolVersion: '2024-11-05',
                capabilities: {
                    tools: {}
                },
                serverInfo: {
                    name: 'xiaozhi-mcp-pc-bridge',
                    version: '1.0.0'
                }
            }
        };
        send(response);
    } else if (method === 'tools/list') {
        const response = {
            jsonrpc: '2.0',
            id: id,
            result: {
                tools: [
                    {
                        name: 'web_search',
                        description: 'Tìm kiếm thông tin tổng hợp trên mạng Internet bằng công cụ DuckDuckGo (Hỗ trợ tiếng Việt và tiếng Anh). Dùng để cập nhật tin tức mới, tra cứu sự kiện hiện tại.',
                        inputSchema: {
                            type: 'object',
                            properties: {
                                query: {
                                    type: 'string',
                                    description: 'Từ khóa tìm kiếm trên Internet'
                                }
                            },
                            required: ['query']
                        }
                    },
                    {
                        name: 'wikipedia_search',
                        description: 'Tra cứu bách khoa toàn thư Wikipedia tiếng Việt về nhân vật, sự kiện lịch sử, khái niệm khoa học, địa danh.',
                        inputSchema: {
                            type: 'object',
                            properties: {
                                query: {
                                    type: 'string',
                                    description: 'Từ khóa hoặc tên bài viết cần tra cứu trên Wikipedia'
                                }
                            },
                            required: ['query']
                        }
                    },
                    {
                        name: 'get_pc_time',
                        description: 'Lấy ngày giờ hiện tại trên máy tính của người dùng.',
                        inputSchema: {
                            type: 'object',
                            properties: {}
                        }
                    },
                    {
                        name: 'play_youtube',
                        description: 'CHỈ SỬ DỤNG KHI người dùng yêu cầu rõ ràng "mở trên máy tính" hoặc "mở trên PC". Không dùng tool này cho các lệnh "phát nhạc" thông thường.',
                        inputSchema: {
                            type: 'object',
                            properties: {
                                query: {
                                    type: 'string',
                                    description: 'Tên bài hát, ca sĩ hoặc nội dung cần tìm kiếm để phát trên YouTube'
                                }
                            },
                            required: ['query']
                        }
                    },
                    {
                        name: 'stream_youtube_audio_to_esp32',
                        description: 'Đây là TOOL ƯU TIÊN khi người dùng yêu cầu "phát nhạc", "bật nhạc", "nghe nhạc". Tìm bài hát trên YouTube và phát nhạc TRỰC TIẾP trên loa của thiết bị IoT (ESP32 box).',
                        inputSchema: {
                            type: 'object',
                            properties: {
                                query: {
                                    type: 'string',
                                    description: 'Tên bài hát, ca sĩ hoặc nội dung cần tìm để phát nhạc'
                                }
                            },
                            required: ['query']
                        }
                    },
                    {
                        name: 'stop_music_on_box',
                        description: 'Dừng phát nhạc trên thiết bị IoT (ESP32 box). Dùng khi người dùng muốn tắt nhạc, ví dụ: "tắt nhạc", "dừng nhạc", "stop".',
                        inputSchema: {
                            type: 'object',
                            properties: {}
                        }
                    }
                ]
            }
        };
        send(response);
    } else if (method === 'tools/call') {
        const toolName = params.name;
        const args = params.arguments || {};
        console.log(`[CALL TOOL] ${toolName} with args:`, args);

        let toolResult = null;
        try {
            if (toolName === 'web_search') {
                const searchResults = await searchDuckDuckGo(args.query);
                toolResult = formatSearchResults(searchResults);
            } else if (toolName === 'wikipedia_search') {
                toolResult = await searchWikipedia(args.query);
            } else if (toolName === 'get_pc_time') {
                toolResult = formatPcTime();
            } else if (toolName === 'play_youtube') {
                toolResult = await playYoutube(args.query);
            } else if (toolName === 'stream_youtube_audio_to_esp32') {
                toolResult = await playMusicOnBox(args.query);
            } else if (toolName === 'stop_music_on_box') {
                toolResult = await stopMusicOnBox();
            } else {
                throw new Error(`Tool ${toolName} not found`);
            }

            const response = {
                jsonrpc: '2.0',
                id: id,
                result: {
                    content: [
                        {
                            type: 'text',
                            text: toolResult
                        }
                    ]
                }
            };
            send(response);
        } catch (err) {
            console.error(`Error executing tool ${toolName}:`, err);
            const response = {
                jsonrpc: '2.0',
                id: id,
                error: {
                    code: -32603,
                    message: `Internal error: ${err.message}`
                }
            };
            send(response);
        }
    }
}

function send(data) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(data));
        console.log(`[OUTGOING] Replied to request ${data.id}`);
    }
}

// ----------------------------------------------------
// Tool Implementations
// ----------------------------------------------------

// 1. DuckDuckGo Scraper (Keyless & Free)
function searchDuckDuckGo(query) {
    return new Promise((resolve, reject) => {
        const url = `https://html.duckduckgo.com/html/?q=${encodeURIComponent(query)}`;
        const options = {
            headers: {
                'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36'
            }
        };
        https.get(url, options, (res) => {
            let data = '';
            res.on('data', (chunk) => { data += chunk; });
            res.on('end', () => {
                const results = [];
                const parts = data.split('class="result results_links');
                for (let i = 1; i < parts.length && results.length < 5; i++) {
                    const part = parts[i];
                    
                    // 1. Extract link
                    const linkMatch = part.match(/href="(?:\/\/duckduckgo\.com)?\/l\/\?uddg=([^&"]+)/);
                    let link = '';
                    if (linkMatch) {
                        link = decodeURIComponent(linkMatch[1]);
                    }
                    
                    // 2. Extract title
                    const titleMatch = part.match(/class="result__a"[^>]*>([\s\S]*?)<\/a>/);
                    let title = 'No Title';
                    if (titleMatch) {
                        title = titleMatch[1].replace(/<[^>]*>/g, '').replace(/\s+/g, ' ').trim();
                    }
                    
                    // 3. Extract snippet
                    const snippetMatch = part.match(/class="result__snippet"[^>]*>([\s\S]*?)<\/a>/);
                    let snippet = '';
                    if (snippetMatch) {
                        snippet = snippetMatch[1].replace(/<[^>]*>/g, '').replace(/\s+/g, ' ').trim();
                    }
                    
                    if (link && title !== 'No Title') {
                        results.push({ title, link, snippet });
                    }
                }
                resolve(results);
            });
        }).on('error', (err) => {
            reject(err);
        });
    });
}

function formatSearchResults(results) {
    if (results.length === 0) {
        return "Không tìm thấy kết quả nào trên Internet.";
    }
    let output = "Kết quả tìm kiếm Internet:\n\n";
    results.forEach((r, idx) => {
        output += `[${idx + 1}] Tiêu đề: ${r.title}\n`;
        output += `    Nguồn: ${r.link}\n`;
        output += `    Tóm tắt: ${r.snippet}\n\n`;
    });
    return output;
}

// 2. Wikipedia Search API
function searchWikipedia(query) {
    return new Promise((resolve) => {
        // Step 1: Search Wikipedia for pages
        const searchUrl = `https://vi.wikipedia.org/w/api.php?action=query&list=search&srsearch=${encodeURIComponent(query)}&utf8=1&format=json`;
        
        https.get(searchUrl, (res) => {
            let data = '';
            res.on('data', (chunk) => { data += chunk; });
            res.on('end', () => {
                try {
                    const json = JSON.parse(data);
                    const searchResults = json.query.search;
                    if (!searchResults || searchResults.length === 0) {
                        return resolve(`Không tìm thấy thông tin nào về "${query}" trên Wikipedia tiếng Việt.`);
                    }

                    // Get the most relevant page title
                    const bestTitle = searchResults[0].title;
                    console.log(`[WIKIPEDIA] Best match title: ${bestTitle}`);

                    // Step 2: Fetch the intro summary of the matched page
                    const extractUrl = `https://vi.wikipedia.org/w/api.php?action=query&prop=extracts&exintro=1&explaintext=1&titles=${encodeURIComponent(bestTitle)}&format=json`;
                    
                    https.get(extractUrl, (res2) => {
                        let data2 = '';
                        res2.on('data', (chunk) => { data2 += chunk; });
                        res2.on('end', () => {
                            try {
                                const json2 = JSON.parse(data2);
                                const pages = json2.query.pages;
                                const pageId = Object.keys(pages)[0];
                                const extract = pages[pageId].extract;
                                
                                if (extract) {
                                    resolve(`Thông tin tra cứu Wikipedia về "${bestTitle}":\n\n${extract}`);
                                } else {
                                    resolve(`Có bài viết Wikipedia về "${bestTitle}" nhưng không có thông tin tóm tắt. Bạn có thể tra cứu chi tiết tại: https://vi.wikipedia.org/wiki/${encodeURIComponent(bestTitle)}`);
                                }
                            } catch(e) {
                                resolve(`Không thể tải chi tiết bài viết Wikipedia cho "${bestTitle}".`);
                            }
                        });
                    });

                } catch (e) {
                    resolve("Có lỗi xảy ra khi truy vấn dữ liệu từ Wikipedia.");
                }
            });
        }).on('error', (err) => {
            resolve(`Không thể kết nối tới Wikipedia: ${err.message}`);
        });
    });
}

// 3. Current Host PC Time
function formatPcTime() {
    const now = new Date();
    const days = ['Chủ Nhật', 'Thứ Hai', 'Thứ Ba', 'Thứ Tư', 'Thứ Năm', 'Thứ Sáu', 'Thứ Bảy'];
    const dayName = days[now.getDay()];
    return `Giờ hệ thống máy tính hiện tại: ${now.toLocaleTimeString('vi-VN')} - ${dayName}, ngày ${now.toLocaleDateString('vi-VN')}`;
}

// 4. YouTube Player Tool (Local execution via Windows Browser)
function searchYoutube(query) {
    return new Promise((resolve, reject) => {
        const url = `https://www.youtube.com/results?search_query=${encodeURIComponent(query)}`;
        const options = {
            headers: {
                'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
                'Accept-Language': 'vi,en;q=0.9'
            }
        };
        https.get(url, options, (res) => {
            let data = '';
            res.on('data', (chunk) => { data += chunk; });
            res.on('end', () => {
                const regex = /\/watch\?v=([a-zA-Z0-9_-]{11})/g;
                let match;
                const ids = [];
                while ((match = regex.exec(data)) !== null && ids.length < 5) {
                    if (!ids.includes(match[1])) {
                        ids.push(match[1]);
                    }
                }
                if (ids.length > 0) {
                    resolve(ids[0]);
                } else {
                    reject(new Error("Không tìm thấy video nào trên YouTube."));
                }
            });
        }).on('error', (err) => {
            reject(err);
        });
    });
}

function playYoutube(query) {
    const { exec } = require('child_process');
    return new Promise((resolve) => {
        searchYoutube(query)
            .then(videoId => {
                const url = `https://www.youtube.com/watch?v=${videoId}`;
                console.log(`[YOUTUBE] Opening URL: ${url}`);
                
                exec(`start "" "${url}"`, (err) => {
                    if (err) {
                        resolve(`Lỗi khi mở trình duyệt để phát nhạc: ${err.message}`);
                    } else {
                        resolve(`Đã tìm thấy và đang phát bài hát trên YouTube qua trình duyệt máy tính của bạn: ${url}`);
                    }
                });
            })
            .catch(err => {
                resolve(`Không thể phát nhạc trên YouTube: ${err.message}`);
            });
    });
}

// 5. Play Music on ESP32 Box via yt-dlp
let ESP32_IP = ''; // Will be auto-detected

function findEsp32Ip() {
    return new Promise((resolve) => {
        // Try common IPs or use stored one
        if (ESP32_IP) return resolve(ESP32_IP);
        
        // Try to detect from network - check common ranges
        const tryIp = (ip) => {
            return new Promise((res) => {
                const req = http.get(`http://${ip}/api/status`, { timeout: 1000 }, (response) => {
                    let data = '';
                    response.on('data', (chunk) => data += chunk);
                    response.on('end', () => {
                        try {
                            JSON.parse(data);
                            res(ip);
                        } catch { res(null); }
                    });
                });
                req.on('error', () => res(null));
                req.on('timeout', () => { req.destroy(); res(null); });
            });
        };
        
        // Check if user set IP via environment variable
        if (process.env.ESP32_IP) {
            ESP32_IP = process.env.ESP32_IP;
            return resolve(ESP32_IP);
        }
        
        // Try common IPs
        const commonIps = [];
        for (let i = 1; i <= 254; i++) {
            commonIps.push(`192.168.1.${i}`);
        }
        
        // Quick scan - try first 50 in parallel batches
        const batchSize = 30;
        let found = false;
        const scanBatch = async (start) => {
            if (found || start >= commonIps.length) return null;
            const batch = commonIps.slice(start, start + batchSize);
            const results = await Promise.all(batch.map(tryIp));
            const validIp = results.find(r => r !== null);
            if (validIp) {
                found = true;
                ESP32_IP = validIp;
                console.log(`[MUSIC] Auto-detected ESP32 at ${validIp}`);
                return validIp;
            }
            return scanBatch(start + batchSize);
        };
        
        scanBatch(0).then(ip => resolve(ip || null));
    });
}

function getYtDlpPath() {
    // Try to find yt-dlp
    try {
        const result = execSync('where yt-dlp 2>nul || python -m yt_dlp --version 2>nul', { encoding: 'utf-8', timeout: 5000 });
        if (result.includes('yt-dlp')) return 'yt-dlp';
    } catch {}
    // Try python module
    return 'python -m yt_dlp';
}

async function playMusicOnBox(query) {
    console.log(`[MUSIC] Searching for: ${query}`);
    
    // Step 1: Find ESP32 IP
    const ip = await findEsp32Ip();
    if (!ip) {
        return 'Không tìm thấy thiết bị ESP32 trong mạng LAN. Hãy đảm bảo thiết bị đang bật và kết nối cùng WiFi.';
    }
    
    // Step 2: Search YouTube and get audio URL via yt-dlp
    const ytdlp = getYtDlpPath();
    
    return new Promise((resolve) => {
        // Search YouTube for the song and get best audio URL
        const cmd = `${ytdlp} -f "ba[ext=m4a]/ba" --get-url --no-playlist --default-search "ytsearch" "${query.replace(/"/g, '\\"')}"  2>nul`;
        console.log(`[MUSIC] Running: ${cmd}`);
        
        exec(cmd, { timeout: 30000, encoding: 'utf-8' }, (err, stdout, stderr) => {
            if (err || !stdout.trim()) {
                console.error(`[MUSIC] yt-dlp error:`, err?.message || stderr);
                resolve(`Không tìm thấy bài hát "${query}" trên YouTube. Lỗi: ${err?.message || 'Không có kết quả'}`);
                return;
            }
            
            const audioUrl = stdout.trim().split('\n')[0];
            console.log(`[MUSIC] Got audio URL (${audioUrl.substring(0, 80)}...)`);
            
            // Step 3: Also get video title
            exec(`${ytdlp} --get-title --no-playlist --default-search "ytsearch" "${query.replace(/"/g, '\\"')}" 2>nul`, 
                { timeout: 15000, encoding: 'utf-8' }, 
                (err2, titleOut) => {
                    const title = titleOut?.trim()?.split('\n')[0] || query;
                    
                    // Step 4: Send URL to ESP32
                    const postData = JSON.stringify({ url: audioUrl, title: title });
                    const options = {
                        hostname: ip,
                        port: 80,
                        path: '/api/play_music',
                        method: 'POST',
                        headers: {
                            'Content-Type': 'application/json',
                            'Content-Length': Buffer.byteLength(postData)
                        },
                        timeout: 5000
                    };
                    
                    const req = http.request(options, (res) => {
                        let data = '';
                        res.on('data', (chunk) => data += chunk);
                        res.on('end', () => {
                            console.log(`[MUSIC] ESP32 response: ${data}`);
                            resolve(`Đang phát bài "${title}" trên loa thiết bị IoT!`);
                        });
                    });
                    
                    req.on('error', (e) => {
                        console.error(`[MUSIC] Failed to send to ESP32:`, e.message);
                        resolve(`Tìm thấy bài "${title}" nhưng không thể gửi đến thiết bị: ${e.message}`);
                    });
                    
                    req.write(postData);
                    req.end();
                }
            );
        });
    });
}

async function stopMusicOnBox() {
    const ip = await findEsp32Ip();
    if (!ip) return 'Không tìm thấy thiết bị ESP32.';
    
    return new Promise((resolve) => {
        const req = http.request({
            hostname: ip,
            port: 80,
            path: '/api/stop_music',
            method: 'POST',
            timeout: 3000
        }, (res) => {
            res.on('data', () => {});
            res.on('end', () => resolve('Đã dừng phát nhạc trên thiết bị.'));
        });
        req.on('error', (e) => resolve(`Lỗi: ${e.message}`));
        req.end();
    });
}

// Start
connect();
