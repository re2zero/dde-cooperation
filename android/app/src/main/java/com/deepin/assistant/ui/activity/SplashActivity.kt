package com.deepin.assistant.ui.activity

import android.animation.*
import android.content.Intent
import android.view.*
import com.airbnb.lottie.LottieAnimationView
import com.gyf.immersionbar.BarHide
import com.gyf.immersionbar.ImmersionBar
import com.deepin.assistant.R
import com.deepin.assistant.app.AppActivity
import com.deepin.assistant.http.api.UserInfoApi
import com.deepin.assistant.http.model.HttpData
import com.deepin.assistant.other.AppConfig
import com.hjq.http.EasyHttp
import com.hjq.http.listener.HttpCallback
import com.hjq.widget.view.SlantedTextView
import java.util.*

/**
 *    author : Android 轮子哥
 *    github : https://github.com/getActivity/AndroidProject-Kotlin
 *    time   : 2018/10/18
 *    desc   : 闪屏界面
 */
class SplashActivity : AppActivity() {

    private val lottieView: LottieAnimationView? by lazy { findViewById(R.id.lav_splash_lottie) }
    private val debugView: SlantedTextView? by lazy { findViewById(R.id.iv_splash_debug) }

    override fun getLayoutId(): Int {
        return R.layout.splash_activity
    }

    override fun initView() {
        // 设置动画监听
        lottieView?.addAnimatorListener(object : AnimatorListenerAdapter() {
            override fun onAnimationEnd(animation: Animator?) {
                lottieView?.removeAnimatorListener(this)
                MainActivity.start(this@SplashActivity)
                finish()
            }
        })
    }

    override fun initData() {
        debugView?.let {
            it.setText(AppConfig.getBuildType().uppercase(Locale.getDefault()))
            if (AppConfig.isDebug()) {
                it.visibility = View.VISIBLE
            } else {
                it.visibility = View.INVISIBLE
            }
        }

//        if (true) {
//            return
//        }
//        // 刷新用户信息
//        EasyHttp.post(this)
//            .api(UserInfoApi())
//            .request(object : HttpCallback<HttpData<UserInfoApi.Bean?>>(this) {
//
//                override fun onSucceed(data: HttpData<UserInfoApi.Bean?>) {
//
//                }
//            })
    }

    override fun createStatusBarConfig(): ImmersionBar {
        return super.createStatusBarConfig()
            // 隐藏状态栏和导航栏
            .hideBar(BarHide.FLAG_HIDE_BAR)
    }

    override fun onBackPressed() {
        // 禁用返回键
        //super.onBackPressed();
    }

    override fun initActivity() {
        // 问题及方案：https://www.cnblogs.com/net168/p/5722752.html
        // 如果当前 Activity 不是任务栈中的第一个 Activity
        if (!isTaskRoot) {
            val intent: Intent? = intent
            // 如果当前 Activity 是通过桌面图标启动进入的
            if (((intent != null) && intent.hasCategory(Intent.CATEGORY_LAUNCHER)
                        && (Intent.ACTION_MAIN == intent.action))) {
                // 对当前 Activity 执行销毁操作，避免重复实例化入口
                finish()
                return
            }
        }
        super.initActivity()
    }

}