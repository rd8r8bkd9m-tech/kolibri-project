import React, { useEffect, useRef } from "react";

const WGSL_SHADER = `
struct Particle {
    pos: vec2<f32>,
    vel: vec2<f32>,
    role: f32, // 0 = Learner, 1 = Anchor, 2 = Validator
    energy: f32
};

@group(0) @binding(0) var<storage, read_write> particles: array<Particle>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let i = global_id.x;
    if (i >= arrayLength(&particles)) {
        return;
    }
    
    var p = particles[i];
    
    // Simple flocking / neural swarm simulation step
    p.pos = p.pos + p.vel * 0.01;
    
    // Bounce off walls (-1 to 1)
    if (p.pos.x > 1.0 || p.pos.x < -1.0) { p.vel.x = -p.vel.x; }
    if (p.pos.y > 1.0 || p.pos.y < -1.0) { p.vel.y = -p.vel.y; }
    
    particles[i] = p;
}
`;

export function KnowledgeCanvas() {
    const canvasRef = useRef<HTMLCanvasElement>(null);

    useEffect(() => {
        let animationFrameId: number;
        
        async function initWebGPU() {
            if (!(navigator as any).gpu) {
                console.warn("WebGPU not supported on this browser.");
                return;
            }
            
            const adapter = await (navigator as any).gpu.requestAdapter();
            if (!adapter) return;
            const device = await adapter.requestDevice();
            
            const canvas = canvasRef.current;
            if (!canvas) return;
            const context = canvas.getContext("webgpu") as any;
            if (!context) return;
            
            const presentationFormat = (navigator as any).gpu.getPreferredCanvasFormat();
            context.configure({
                device,
                format: presentationFormat,
                alphaMode: "premultiplied",
            });
            
            // Phase 4: Swarm 50 Neural Telescope Setup
            const numParticles = 50; 
            const particleData = new Float32Array(numParticles * 6); // pos.x, pos.y, vel.x, vel.y, role, energy
            for (let i = 0; i < numParticles; i++) {
                particleData[i * 6 + 0] = (Math.random() - 0.5) * 2;
                particleData[i * 6 + 1] = (Math.random() - 0.5) * 2;
                particleData[i * 6 + 2] = (Math.random() - 0.5) * 0.1;
                particleData[i * 6 + 3] = (Math.random() - 0.5) * 0.1;
                particleData[i * 6 + 4] = Math.floor(Math.random() * 3); // Role
                particleData[i * 6 + 5] = 1.0; // Energy
            }

            const particleBuffer = device.createBuffer({
                size: particleData.byteLength,
                usage: (window as any).GPUBufferUsage.STORAGE | (window as any).GPUBufferUsage.COPY_DST | (window as any).GPUBufferUsage.VERTEX,
            });
            device.queue.writeBuffer(particleBuffer, 0, particleData);

            const computeModule = device.createShaderModule({ code: WGSL_SHADER });
            const computePipeline = device.createComputePipeline({
                layout: 'auto',
                compute: { module: computeModule, entryPoint: "main" }
            });

            const bindGroup = device.createBindGroup({
                layout: computePipeline.getBindGroupLayout(0),
                entries: [{ binding: 0, resource: { buffer: particleBuffer } }]
            });

            // Basic render pipeline (points)
            const renderModule = device.createShaderModule({
                code: `
                    struct Particle {
                        pos: vec2<f32>,
                        vel: vec2<f32>,
                        role: f32,
                        energy: f32
                    };
                    @group(0) @binding(0) var<storage, read> particles: array<Particle>;
                    
                    struct VertexOutput {
                        @builtin(position) position: vec4<f32>,
                        @location(0) color: vec4<f32>
                    };

                    @vertex
                    fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
                        var out: VertexOutput;
                        let p = particles[vertexIndex];
                        out.position = vec4<f32>(p.pos, 0.0, 1.0);
                        
                        if (p.role == 0.0) {
                            out.color = vec4<f32>(0.2, 0.8, 0.2, 1.0); // Learner (Green)
                        } else if (p.role == 1.0) {
                            out.color = vec4<f32>(0.2, 0.2, 0.8, 1.0); // Anchor (Blue)
                        } else {
                            out.color = vec4<f32>(0.8, 0.2, 0.2, 1.0); // Validator (Red)
                        }
                        return out;
                    }

                    @fragment
                    fn fs_main(@location(0) color: vec4<f32>) -> @location(0) vec4<f32> {
                        return color;
                    }
                `
            });

            const renderPipeline = device.createRenderPipeline({
                layout: 'auto',
                vertex: { module: renderModule, entryPoint: 'vs_main' },
                fragment: { module: renderModule, entryPoint: 'fs_main', targets: [{ format: presentationFormat }] },
                primitive: { topology: 'point-list' }
            });

            function frame() {
                const commandEncoder = device.createCommandEncoder();
                
                // Compute pass
                const computePass = commandEncoder.beginComputePass();
                computePass.setPipeline(computePipeline);
                computePass.setBindGroup(0, bindGroup);
                computePass.dispatchWorkgroups(Math.ceil(numParticles / 64));
                computePass.end();

                // Render pass
                const textureView = context.getCurrentTexture().createView();
                const renderPass = commandEncoder.beginRenderPass({
                    colorAttachments: [{
                        view: textureView,
                        clearValue: { r: 0.05, g: 0.05, b: 0.05, a: 1.0 },
                        loadOp: 'clear',
                        storeOp: 'store',
                    }]
                });
                renderPass.setPipeline(renderPipeline);
                renderPass.setBindGroup(0, bindGroup);
                renderPass.draw(numParticles, 1, 0, 0);
                renderPass.end();

                device.queue.submit([commandEncoder.finish()]);
                animationFrameId = requestAnimationFrame(frame);
            }
            
            frame();
        }
        
        initWebGPU();
        
        return () => {
            if (animationFrameId) cancelAnimationFrame(animationFrameId);
        };
    }, []);

    return (
        <div style={{ position: 'relative', width: '100%', height: '400px', borderRadius: '8px', overflow: 'hidden' }}>
            <canvas ref={canvasRef} width={800} height={400} style={{ width: '100%', height: '100%' }} />
            <div style={{ position: 'absolute', top: 10, left: 10, color: 'white', background: 'rgba(0,0,0,0.5)', padding: '4px 8px', borderRadius: '4px', fontSize: '12px' }}>
                Neural Telescope: Swarm 50 (WebGPU)
            </div>
        </div>
    );
}
