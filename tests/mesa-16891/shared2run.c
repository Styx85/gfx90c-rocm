/* Minimal Vulkan compute runner for shared2_wrap.comp.
 *
 * usage: shared2run <spv> [workgroups=256]
 *
 * Creates the compute pipeline first (the assertion fires inside vkCreateComputePipelines).
 * workgroups = 0: stop after pipeline creation (compile-only, e.g. with RADV_FORCE_FAMILY).
 * Otherwise: dispatch <workgroups> groups of 128 invocations with take_oob_path = 0 and check
 * every result word against a CPU oracle: result[gid] = (wg * 128 + 127 - lid) * 3.
 *
 * Build: cc -O2 -o shared2run shared2run.c -lvulkan
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define CK(x)                                                                                      \
   do {                                                                                            \
      VkResult r_ = (x);                                                                           \
      if (r_ != VK_SUCCESS) {                                                                      \
         fprintf(stderr, "%s -> %d\n", #x, r_);                                                    \
         exit(3);                                                                                  \
      }                                                                                            \
   } while (0)

int main(int argc, char **argv)
{
   if (argc < 2) {
      fprintf(stderr, "usage: %s <spv> [workgroups]\n", argv[0]);
      return 2;
   }
   const uint32_t groups = argc > 2 ? (uint32_t)atoi(argv[2]) : 256;
   const uint32_t tgs = 128, words = groups * tgs;

   FILE *f = fopen(argv[1], "rb");
   if (!f) {
      perror(argv[1]);
      return 2;
   }
   fseek(f, 0, SEEK_END);
   long code_size = ftell(f);
   fseek(f, 0, SEEK_SET);
   uint32_t *code = malloc(code_size);
   if (fread(code, 1, code_size, f) != (size_t)code_size)
      return 2;
   fclose(f);

   VkApplicationInfo ai = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1};
   VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &ai};
   VkInstance inst;
   CK(vkCreateInstance(&ici, NULL, &inst));
   uint32_t n = 1;
   VkPhysicalDevice pd;
   vkEnumeratePhysicalDevices(inst, &n, &pd);
   if (!n)
      return 4;
   VkPhysicalDeviceProperties pp;
   vkGetPhysicalDeviceProperties(pd, &pp);

   float prio = 1.0f;
   VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                  .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio};
   VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
                             .pQueueCreateInfos = &qci};
   VkDevice dev;
   CK(vkCreateDevice(pd, &dci, NULL, &dev));

   /* Pipeline: one storage buffer at set 0, binding 0. */
   VkDescriptorSetLayoutBinding lb = {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                      .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo dlci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                           .bindingCount = 1, .pBindings = &lb};
   VkDescriptorSetLayout dsl;
   CK(vkCreateDescriptorSetLayout(dev, &dlci, NULL, &dsl));
   VkPipelineLayoutCreateInfo plci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
                                      .pSetLayouts = &dsl};
   VkPipelineLayout pl;
   CK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
   VkShaderModuleCreateInfo smci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                    .codeSize = (size_t)code_size, .pCode = code};
   VkShaderModule mod;
   CK(vkCreateShaderModule(dev, &smci, NULL, &mod));
   VkComputePipelineCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = mod, .pName = "main"},
      .layout = pl};
   VkPipeline pipe;
   printf("creating pipeline on %s\n", pp.deviceName);
   fflush(stdout);
   CK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipe));
   printf("pipeline created\n");
   if (!groups)
      return 0;

   /* Buffer: take_oob_path, pad[3], result[groups * 128]. */
   const VkDeviceSize size = 16 + 4ull * words;
   VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size,
                             .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
   VkBuffer buf;
   CK(vkCreateBuffer(dev, &bci, NULL, &buf));
   VkMemoryRequirements mr;
   vkGetBufferMemoryRequirements(dev, buf, &mr);
   VkPhysicalDeviceMemoryProperties mp;
   vkGetPhysicalDeviceMemoryProperties(pd, &mp);
   const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   uint32_t t = 0;
   while (t < mp.memoryTypeCount && !((mr.memoryTypeBits >> t & 1) && (mp.memoryTypes[t].propertyFlags & want) == want))
      t++;
   VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
                               .memoryTypeIndex = t};
   VkDeviceMemory mem;
   CK(vkAllocateMemory(dev, &mai, NULL, &mem));
   CK(vkBindBufferMemory(dev, buf, mem, 0));
   uint32_t *map;
   CK(vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, (void **)&map));
   memset(map, 0xee, size);
   map[0] = 0; /* take_oob_path = 0 */

   VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
   VkDescriptorPoolCreateInfo dpci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1,
                                      .poolSizeCount = 1, .pPoolSizes = &ps};
   VkDescriptorPool pool;
   CK(vkCreateDescriptorPool(dev, &dpci, NULL, &pool));
   VkDescriptorSetAllocateInfo dsai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                       .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &dsl};
   VkDescriptorSet ds;
   CK(vkAllocateDescriptorSets(dev, &dsai, &ds));
   VkDescriptorBufferInfo dbi = {buf, 0, VK_WHOLE_SIZE};
   VkWriteDescriptorSet wds = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds, .dstBinding = 0,
                               .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                               .pBufferInfo = &dbi};
   vkUpdateDescriptorSets(dev, 1, &wds, 0, NULL);

   VkQueue q;
   vkGetDeviceQueue(dev, 0, 0, &q);
   VkCommandPoolCreateInfo cpi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0};
   VkCommandPool cpool;
   CK(vkCreateCommandPool(dev, &cpi, NULL, &cpool));
   VkCommandBufferAllocateInfo cbai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                       .commandPool = cpool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                       .commandBufferCount = 1};
   VkCommandBuffer cb;
   CK(vkAllocateCommandBuffers(dev, &cbai, &cb));
   VkCommandBufferBeginInfo cbbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   CK(vkBeginCommandBuffer(cb, &cbbi));
   vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
   vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, NULL);
   vkCmdDispatch(cb, groups, 1, 1);
   VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                         .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
   vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0,
                        NULL);
   CK(vkEndCommandBuffer(cb));
   VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb};
   CK(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE));
   CK(vkQueueWaitIdle(q));

   /* CPU oracle: the guarded branch is not taken, so result[gid] = s[127 - lid] = (wg * 128 + 127 - lid) * 3. */
   uint32_t bad = 0;
   for (uint32_t gid = 0; gid < words; gid++) {
      uint32_t wg = gid / tgs, lid = gid % tgs;
      uint32_t want_v = (wg * tgs + (tgs - 1 - lid)) * 3u, got = map[4 + gid];
      if (got != want_v && bad++ < 4)
         printf("  gid %u: got 0x%08x want 0x%08x\n", gid, got, want_v);
   }
   printf("%u workgroups x %u invocations, %u wrong words: %s\n", groups, tgs, bad, bad ? "WRONG" : "PASS");
   return bad ? 1 : 0;
}
