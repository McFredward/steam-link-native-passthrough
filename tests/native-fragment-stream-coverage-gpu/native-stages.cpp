// Compile private actual native stage pairs on owned Vulkan resources. No draw,
// headset access, source-frame authority or native shader bodies are included.
#include <vulkan/vulkan.h>
namespace native_stages_gpu {
VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice,const VkDeviceCreateInfo*,const VkAllocationCallbacks*,VkDevice*);
}
#define vkCreateDevice native_stages_gpu::createDevice
#define main sfnp_owned_gpu_unused_main
#include "../native-fragment-ssa-gpu/fixture.cpp"
#undef main
#undef vkCreateDevice
#include "../../layer/stream_bridge_coverage_transform.h"
namespace native_stages_gpu {
VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo* supplied,
        const VkAllocationCallbacks* allocator,VkDevice* output){
    std::uint32_t count=0;check(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr));
    std::vector<VkExtensionProperties> properties(count);check(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,properties.data()));
    require(std::any_of(properties.begin(),properties.end(),[](const auto& p){return !std::strcmp(p.extensionName,VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME);}),
            "owned device supports actual native vertex Layer output");
    auto create=*supplied;const char* extension=VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME;
    create.enabledExtensionCount=1;create.ppEnabledExtensionNames=&extension;
    return vkCreateDevice(physical,&create,allocator,output);
}
struct NativeGpu:Gpu {
    std::array<VkDescriptorSetLayout,2> extra{};
    ~NativeGpu(){
        if(pipelineLayout){vkDestroyPipelineLayout(device,pipelineLayout,nullptr);pipelineLayout={};}
        for(auto value:extra)if(value)vkDestroyDescriptorSetLayout(device,value,nullptr);
    }
    void prepare(){
        std::array<VkDescriptorSetLayoutBinding,26> bindings{};
        for(unsigned n=0;n<bindings.size();++n)bindings[n]={n,n<8?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            n==8?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:n<25?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLER,
            n==9?3u:1u,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};set.bindingCount=bindings.size();set.pBindings=bindings.data();
        check(vkCreateDescriptorSetLayout(device,&set,nullptr,&descriptorLayout));
        VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        set.bindingCount=1;set.pBindings=&binding;check(vkCreateDescriptorSetLayout(device,&set,nullptr,&extra[0]));
        binding.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;check(vkCreateDescriptorSetLayout(device,&set,nullptr,&extra[1]));
        const std::array<VkDescriptorSetLayout,3> sets{{descriptorLayout,extra[0],extra[1]}};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=sets.size();layout.pSetLayouts=sets.data();
        check(vkCreatePipelineLayout(device,&layout,nullptr,&pipelineLayout));
        VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R32G32B32A32_SFLOAT;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass{};
        subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&color;
        VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};pass.attachmentCount=1;pass.pAttachments=&attachment;
        pass.subpassCount=1;pass.pSubpasses=&subpass;check(vkCreateRenderPass(device,&pass,nullptr,&renderPass));
    }
    void compile(VkShaderModule vertex,VkShaderModule fragment){
        std::array<VkPipelineShaderStageCreateInfo,2> stages{};for(auto& s:stages)s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[0].pName="VS_DISTORT";
        stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;stages[1].pName="PS_DISTORT";
        VkVertexInputBindingDescription binding{0,40,VK_VERTEX_INPUT_RATE_VERTEX};
        const std::array<VkVertexInputAttributeDescription,4> attributes{{{0,0,VK_FORMAT_R32G32B32A32_SFLOAT,0},
            {1,0,VK_FORMAT_R32G32_SFLOAT,16},{2,0,VK_FORMAT_R32G32_SFLOAT,24},{3,0,VK_FORMAT_R32G32_SFLOAT,32}}};
        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount=1;vertexInput.pVertexBindingDescriptions=&binding;
        vertexInput.vertexAttributeDescriptionCount=attributes.size();vertexInput.pVertexAttributeDescriptions=attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0,0,16,16,0,1};VkRect2D scissor{{0,0},{16,16}};
        VkPipelineViewportStateCreateInfo view{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};view.viewportCount=view.scissorCount=1;view.pViewports=&viewport;view.pScissors=&scissor;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};samples.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState color{};color.colorWriteMask=15;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&color;
        VkGraphicsPipelineCreateInfo create{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};create.stageCount=stages.size();create.pStages=stages.data();
        create.pVertexInputState=&vertexInput;create.pInputAssemblyState=&assembly;create.pViewportState=&view;
        create.pRasterizationState=&raster;create.pMultisampleState=&samples;create.pColorBlendState=&blend;
        create.layout=pipelineLayout;create.renderPass=renderPass;
        VkPipeline result{};check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&create,nullptr,&result));
        require(result!=VK_NULL_HANDLE,"actual native stages compile with owned descriptor/render state");
        vkDestroyPipeline(device,result,nullptr);
    }
};
}
int main(int argc,char** argv){try{
    require(argc==5,"usage: native-stages VERTEX ORIGINAL COVERAGE CAMERA_ONLY");
    auto vs=shader(argv[1]),ps=shader(argv[2]);
    namespace c=frame_pixel_ssbo_clone;c::ShaderDefinition vertex,fragment;c::Refusal why;
    VkShaderModuleCreateInfo source{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};source.codeSize=vs.size()*4;source.pCode=vs.data();
    require(c::captureShader(source,c::ShaderRole::Vertex,vertex,why),"reviewed actual vertex logic");
    source.codeSize=ps.size()*4;source.pCode=ps.data();require(c::captureShader(source,c::ShaderRole::Fragment,fragment,why),"reviewed actual original fragment logic");
    require(stream_bridge_coverage_transform::pairedStages(vertex,fragment),"actual native pair and varying interface admitted before any GPU pipeline create");
    native_stages_gpu::NativeGpu gpu;gpu.init();require(gpu.validationEnabled,"actual Vulkan validation enabled");gpu.prepare();
    const auto vertexModule=gpu.module(vs);
    for(unsigned n=2;n<5;++n){const auto fragmentModule=gpu.module(shader(argv[n]));gpu.compile(vertexModule,fragmentModule);vkDestroyShaderModule(gpu.device,fragmentModule,nullptr);}
    vkDestroyShaderModule(gpu.device,vertexModule,nullptr);
    require(validationErrors.load()==0,"zero actual native stage pipeline validation errors");
    std::cout<<"{\"scope\":\"actual-native-stage-pipeline\",\"pipelines\":3,\"checks\":"<<checks<<",\"validation_errors\":"<<validationErrors.load()<<",\"draws\":0,\"headset_test\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
