// Use the real MCP transport from a terminal while the desktop connection reloads.
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';
import { fileURLToPath } from 'node:url';
const client = new Client({name:'bpp-debug-cli',version:'1.0.0'});
try {
  await client.connect(new StdioClientTransport({command:process.execPath,args:[fileURLToPath(new URL('./server.mjs',import.meta.url))],stderr:'inherit'}));
  const name=process.argv[2];
  const result=name ? await client.callTool({name,arguments:JSON.parse(process.argv[3] || '{}')}) : await client.listTools();
  process.stdout.write(JSON.stringify(result,null,2)+'\n');
  if(result.isError)process.exitCode=1;
} finally { await client.close(); }
