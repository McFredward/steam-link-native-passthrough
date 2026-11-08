// Actual current native fragment on owned offscreen resources. Original RGB
// and its consumed reconstruction weight are independent GPU oracles.
#define SFNP_COVERAGE_FIXTURE_NO_MAIN
#include "fixture.cpp"
namespace {
struct CurrentGpu:CoverageGpu {
    void currentMetadata(const Case& c){
        metadata(c);
        std::memset(buffers[b4].mapped,0,272);
        const std::array<float,4> pictureParameters{{1,1,1,1}};
        std::memcpy(static_cast<char*>(buffers[b4].mapped)+176,pictureParameters.data(),16);
        std::memcpy(static_cast<char*>(buffers[b4].mapped)+176,&c.flags,4);
        std::memcpy(static_cast<char*>(buffers[b4].mapped)+192,c.tint.data(),16);
        flush(b4);
    }
    std::array<std::uint32_t,4> draw(const Case& c,VkPipeline pipeline,float exact=0,float recovery=0,bool mask=false){
        const std::array<float,4> controlValues{{exact,recovery,0,0}};
        std::memcpy(buffers[control].mapped,controlValues.data(),sizeof controlValues);flush(control);
        currentMetadata(c);startPass();
        if(mask)vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,1,1,&maskSet,0,nullptr);
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,2,1,&controlSet,0,nullptr);
        raster(pipeline,c.u);return readPass();
    }
};
float sample(float x,float y){
    (void)y;
    const unsigned column=unsigned(std::max(0,std::min(15,int(std::floor(x*16.f)))));
    return std::array<unsigned,4>{{0,64,127,255}}[column/4]/255.f;
}
}
#ifndef SFNP_CURRENT_COVERAGE_FIXTURE_NO_MAIN
int main(int argc,char** argv){try{
    require(argc==6,"current fixture: ownedVS originalPS coveragePS cameraPS privateWeightOraclePS");
    CurrentGpu gpu,oracle,camera;
    for(auto* p:{&gpu,&oracle,&camera}){p->init();require(p->validationEnabled,"Khronos validation active");p->singleResources(p==&oracle?3:1);p->prepareMask();}
    gpu.pipelinesSingle(argv[1],argv[2],argv[3],true);
    oracle.pipelinesSingle(argv[1],argv[2],argv[5]);
    camera.pipelinesSingle(argv[1],argv[2],argv[4],true);
    unsigned cases=0,visible=0,transparentEmission=0;
    for(unsigned flags:{0u,2u<<16,0x02018018u})for(float x:{-.25f,.15625f,.40625f,.65625f,.90625f,1.25f})
      for(float innerX:{-.25f,.15625f,.90625f,1.25f})for(float y:{.5f,.99f,1.25f}){
        Case c{};c.u={x,y,innerX,.5f};c.tint={.73f,.59f,.91f,1};c.flags=flags;
        const auto native=oracle.draw(c,oracle.original),weight=oracle.draw(c,oracle.probe);
        const auto covered=gpu.draw(c,gpu.probe,1,0,true);
        require(std::equal(native.begin(),native.begin()+3,covered.begin()),"new native RGB remains bit identical");
        const float w=floating(weight[2]);require(std::isfinite(w)&&w>=0&&w<=1,"actual native reconstruction weight bounded");
        const float expected=(sample(x,y)*floating(weight[0])*(1-w)+sample(innerX,.5f)*floating(weight[1])*w)*floating(weight[3]);
        if(std::abs(floating(covered[3])-expected)>.000025f)
            std::cerr<<"mask case x="<<x<<" inner="<<innerX<<" weight="<<w<<" actual="<<floating(covered[3])<<" expected="<<expected<<'\n';
        near(floating(covered[3]),expected,"coverage follows original sample branches and actual consumed weight");
        require(gpu.draw(c,gpu.probe,0,0,true)==native,"ordinary mode preserves entire original RGBA");
        require(gpu.draw(c,gpu.probe,0,1,true)==std::array<std::uint32_t,4>{},"lost exact data emits camera only");
        require(gpu.draw(c,gpu.probe,1,1,true)==covered,"recovery does not alter a certified game frame");
        require(camera.draw(c,camera.probe,0,1,false)==std::array<std::uint32_t,4>{},"camera-only shader needs no mask descriptor");
        require(camera.draw(c,camera.probe,0,0,false)==native,"camera grant withdrawal preserves native output");
        const bool rgb=native[0]||native[1]||native[2];visible+=rgb;
        transparentEmission+=rgb&&floating(covered[3])==0;++cases;
    }
    if(!visible||!transparentEmission)std::cerr<<"visible cases="<<visible<<" transparent RGB cases="<<transparentEmission<<'\n';
    require(visible>0&&transparentEmission>0,"test exercises real RGB and nonzero RGB at zero coverage");
    require(validationErrors.load()==0,"zero Vulkan validation errors");
    std::cout<<"{\"current_native_cases\":"<<cases<<",\"checks\":"<<checks<<",\"rgb_bit_identical\":true,\"validation_errors\":0,\"headset_test\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#endif
